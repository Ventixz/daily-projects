"""The broker: topics made of partition logs, consumer-group coordination, committed offsets,
and an optional follower mode that replicates another broker."""

from __future__ import annotations

import json
import os
import re
import shutil
import socket
import socketserver
import threading
import time
import uuid
import zlib

from .log import OffsetOutOfRange, PartitionLog, Record
from .protocol import BrokerError, ProtocolError, b64, recv_frame, send_frame, unb64

TOPIC_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$")
MAX_PARTITIONS = 256
MAX_WAIT_MS = 30_000


def partition_for(key: bytes, partitions: int) -> int:
    """Same key -> same partition, which is what gives per-key ordering."""
    return zlib.crc32(key) % partitions


def range_assign(members: list[str], subscriptions: dict[str, list[str]], partitions: dict[str, int]):
    """Kafka's 'range' strategy: for each topic, split its partitions into contiguous
    chunks over the (sorted) members subscribed to it; earlier members get the extras."""
    out: dict[str, list[tuple[str, int]]] = {m: [] for m in members}
    for topic in sorted(partitions):
        subs = sorted(m for m in members if topic in subscriptions[m])
        if not subs:
            continue
        n = partitions[topic]
        per, extra = divmod(n, len(subs))
        p = 0
        for i, m in enumerate(subs):
            take = per + (1 if i < extra else 0)
            out[m].extend((topic, q) for q in range(p, p + take))
            p += take
    return out


class OffsetStore:
    """Committed offsets as an append-only JSON-lines file; the last line for a key wins.
    Compacted (rewritten) on startup so it doesn't grow forever."""

    def __init__(self, path: str):
        self.path = path
        self.lock = threading.Lock()
        self.offsets: dict[tuple[str, str, int], int] = {}
        if os.path.exists(path):
            with open(path, "rb") as f:
                for line in f:
                    try:
                        e = json.loads(line)
                        self.offsets[(e["g"], e["t"], e["p"])] = e["o"]
                    except (ValueError, KeyError, TypeError):
                        continue  # torn last line after a crash
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            for (g, t, p), o in self.offsets.items():
                f.write(json.dumps({"g": g, "t": t, "p": p, "o": o}) + "\n")
        os.replace(tmp, path)
        self._f = open(path, "a")

    def commit(self, group: str, entries: list[tuple[str, int, int]]) -> None:
        with self.lock:
            for t, p, o in entries:
                self.offsets[(group, t, p)] = o
                self._f.write(json.dumps({"g": group, "t": t, "p": p, "o": o}) + "\n")
            self._f.flush()

    def get(self, group: str, topic: str, partition: int) -> int | None:
        with self.lock:
            return self.offsets.get((group, topic, partition))

    def close(self) -> None:
        self._f.close()


class Group:
    def __init__(self, name: str):
        self.name = name
        self.generation = 0
        self.members: dict[str, dict] = {}  # member_id -> {"topics": [...], "seen": monotonic}
        self.assignment: dict[str, list[tuple[str, int]]] = {}


class Coordinator:
    """Consumer groups. Membership changes bump the generation; a member holding an old
    generation is told to rejoin, and its commits are refused (fencing)."""

    def __init__(self, broker: "Broker", session_timeout: float):
        self.broker = broker
        self.session_timeout = session_timeout
        self.groups: dict[str, Group] = {}
        self.lock = threading.Lock()

    def _rebalance(self, g: Group) -> None:
        g.generation += 1
        subs = {m: info["topics"] for m, info in g.members.items()}
        topics = {t for ts in subs.values() for t in ts}
        parts = {t: self.broker.partition_count(t) for t in topics if self.broker.has_topic(t)}
        g.assignment = range_assign(sorted(g.members), subs, parts)

    def _expire(self, g: Group) -> None:
        now = time.monotonic()
        dead = [m for m, info in g.members.items() if now - info["seen"] > self.session_timeout]
        for m in dead:
            del g.members[m]
        if dead:
            self._rebalance(g)

    def sweep(self) -> None:
        with self.lock:
            for g in self.groups.values():
                self._expire(g)

    def join(self, group: str, topics: list[str], member_id: str | None) -> dict:
        with self.lock:
            g = self.groups.setdefault(group, Group(group))
            self._expire(g)
            topics = sorted(set(topics))
            if member_id is None or member_id not in g.members:
                member_id = member_id or f"{group}-{uuid.uuid4().hex[:12]}"
                g.members[member_id] = {"topics": topics, "seen": time.monotonic()}
                self._rebalance(g)
            else:
                info = g.members[member_id]
                info["seen"] = time.monotonic()
                if info["topics"] != topics:
                    info["topics"] = topics
                    self._rebalance(g)
            return self._view(g, member_id)

    def _view(self, g: Group, member_id: str) -> dict:
        return {
            "member_id": member_id,
            "generation": g.generation,
            "assignment": [[t, p] for t, p in g.assignment.get(member_id, [])],
            "members": sorted(g.members),
        }

    def check(self, group: str, member_id: str, generation: int, touch: bool = True) -> None:
        with self.lock:
            g = self.groups.get(group)
            if g is not None:
                self._expire(g)
            if g is None or member_id not in g.members:
                raise BrokerError("unknown_member", "join the group again")
            if touch:
                g.members[member_id]["seen"] = time.monotonic()
            if generation != g.generation:
                raise BrokerError("rebalance_in_progress", f"generation is now {g.generation}")

    def leave(self, group: str, member_id: str) -> None:
        with self.lock:
            g = self.groups.get(group)
            if g and g.members.pop(member_id, None) is not None:
                self._rebalance(g)


class Broker:
    def __init__(
        self,
        data_dir: str,
        segment_bytes: int = 1 << 20,
        retention_bytes: int | None = None,
        session_timeout: float = 10.0,
        leader: tuple[str, int] | None = None,
    ):
        os.makedirs(data_dir, exist_ok=True)
        self.data_dir = data_dir
        self.segment_bytes = segment_bytes
        self.retention_bytes = retention_bytes
        self.leader = leader
        self.lock = threading.Lock()
        self.topics: dict[str, int] = {}
        self.logs: dict[tuple[str, int], PartitionLog] = {}
        self._rr = 0
        meta = os.path.join(data_dir, "topics.json")
        if os.path.exists(meta):
            with open(meta) as f:
                for name, n in json.load(f).items():
                    self._open_topic(name, n)
        self.offsets = OffsetStore(os.path.join(data_dir, "consumer_offsets.jsonl"))
        self.coordinator = Coordinator(self, session_timeout)
        self._server: socketserver.ThreadingTCPServer | None = None
        self._stop = threading.Event()
        self._threads: list[threading.Thread] = []

    # ---- topics -------------------------------------------------------------

    def _open_topic(self, name: str, n: int) -> None:
        # Open every partition before publishing the topic name: other threads check
        # `name in self.topics` without the lock and then index self.logs directly.
        for p in range(n):
            self.logs[(name, p)] = PartitionLog(
                os.path.join(self.data_dir, f"{name}-{p}"),
                segment_bytes=self.segment_bytes,
                retention_bytes=self.retention_bytes,
            )
        self.topics[name] = n

    def _save_meta(self) -> None:
        path = os.path.join(self.data_dir, "topics.json")
        with open(path + ".tmp", "w") as f:
            json.dump(self.topics, f)
        os.replace(path + ".tmp", path)

    def create_topic(self, name: str, partitions: int) -> bool:
        if not isinstance(name, str) or not TOPIC_RE.match(name):
            raise BrokerError("invalid_topic", "use 1-100 chars of [A-Za-z0-9._-], not starting with . _ or -")
        if not isinstance(partitions, int) or not 1 <= partitions <= MAX_PARTITIONS:
            raise BrokerError("invalid_partitions", f"must be 1..{MAX_PARTITIONS}")
        with self.lock:
            if name in self.topics:
                if self.topics[name] != partitions:
                    raise BrokerError("topic_exists", f"{name} has {self.topics[name]} partitions")
                return False
            self._open_topic(name, partitions)
            self._save_meta()
            return True

    def has_topic(self, name: str) -> bool:
        return name in self.topics

    def partition_count(self, name: str) -> int:
        return self.topics[name]

    def log(self, topic, partition) -> PartitionLog:
        if not isinstance(topic, str) or topic not in self.topics:
            raise BrokerError("unknown_topic", str(topic))
        if not isinstance(partition, int) or not 0 <= partition < self.topics[topic]:
            raise BrokerError("unknown_partition", f"{topic}/{partition}")
        return self.logs[(topic, partition)]

    def reset_partition(self, topic: str, partition: int, base: int) -> PartitionLog:
        """Follower only: throw away the local copy and restart it at `base`."""
        with self.lock:
            old = self.logs[(topic, partition)]
            old.close()
            path = old.directory
            shutil.rmtree(path)
            os.makedirs(path)
            from .log import Segment

            Segment(path, base, old.index_interval).close()
            self.logs[(topic, partition)] = PartitionLog(
                path, segment_bytes=self.segment_bytes, retention_bytes=self.retention_bytes
            )
            return self.logs[(topic, partition)]

    # ---- request handling ---------------------------------------------------

    def handle(self, req: dict) -> dict:
        api = req.get("api")
        fn = getattr(self, f"api_{api}", None) if isinstance(api, str) else None
        if fn is None:
            raise BrokerError("unknown_api", str(api))
        return fn(req)

    def api_create_topic(self, req):
        if self.leader:
            raise BrokerError("not_leader", "this broker is a follower")
        return {"created": self.create_topic(req.get("topic"), req.get("partitions", 1))}

    def api_metadata(self, req):
        with self.lock:
            topics = dict(self.topics)
        return {
            "role": "follower" if self.leader else "leader",
            "topics": {
                t: [
                    {"partition": p, "start": self.logs[(t, p)].start_offset, "end": self.logs[(t, p)].end_offset}
                    for p in range(n)
                ]
                for t, n in topics.items()
            },
        }

    def api_produce(self, req):
        if self.leader:
            raise BrokerError("not_leader", "produce to the leader")
        topic = req.get("topic")
        if topic not in self.topics:
            raise BrokerError("unknown_topic", str(topic))
        n = self.topics[topic]
        records = req.get("records")
        if not isinstance(records, list) or not records:
            raise BrokerError("invalid_request", "records must be a non-empty list")
        by_partition: dict[int, list] = {}
        order = []
        for r in records:
            if not isinstance(r, dict) or not isinstance(r.get("value"), str):
                raise BrokerError("invalid_request", "each record needs a base64 'value'")
            key = unb64(r.get("key"))
            value = unb64(r["value"])
            p = req.get("partition")
            if p is None:
                if key is not None:
                    p = partition_for(key, n)
                else:
                    with self.lock:
                        p = self._rr % n
                        self._rr += 1
            self.log(topic, p)  # validates the partition number
            by_partition.setdefault(p, []).append((key, value, None))
            order.append(p)
        try:
            written = {p: iter(self.logs[(topic, p)].append_batch(items)) for p, items in by_partition.items()}
        except ValueError as e:
            raise BrokerError("record_too_large", str(e)) from None
        return {"results": [{"partition": p, "offset": next(written[p])} for p in order]}

    def api_fetch(self, req):
        log = self.log(req.get("topic"), req.get("partition"))
        offset = req.get("offset")
        if not isinstance(offset, int):
            raise BrokerError("invalid_request", "offset must be an integer")
        max_bytes = min(int(req.get("max_bytes", 1 << 20)), 16 << 20)
        wait = max(0, min(int(req.get("max_wait_ms", 0)), MAX_WAIT_MS)) / 1000
        if wait:
            log.wait_for(offset, wait)
        try:
            recs = log.read(offset, max_bytes)
        except OffsetOutOfRange as e:
            raise BrokerError("offset_out_of_range", str(e)) from None
        return {
            "records": [
                {"offset": r.offset, "ts": r.timestamp, "key": b64(r.key), "value": b64(r.value)} for r in recs
            ],
            "start": log.start_offset,
            "end": log.end_offset,
        }

    def api_join_group(self, req):
        group, topics = req.get("group"), req.get("topics")
        if not isinstance(group, str) or not group or not isinstance(topics, list) or not topics:
            raise BrokerError("invalid_request", "need group and topics")
        for t in topics:
            if t not in self.topics:
                raise BrokerError("unknown_topic", str(t))
        return self.coordinator.join(group, topics, req.get("member_id"))

    def api_heartbeat(self, req):
        self.coordinator.check(req.get("group"), req.get("member_id"), req.get("generation"))
        return {}

    def api_leave_group(self, req):
        self.coordinator.leave(req.get("group"), req.get("member_id"))
        return {}

    def api_commit(self, req):
        group = req.get("group")
        if not isinstance(group, str) or not group:
            raise BrokerError("invalid_request", "need group")
        if req.get("member_id") is not None:
            # Fencing: a consumer that missed a rebalance must not overwrite the new owner's progress.
            self.coordinator.check(group, req["member_id"], req.get("generation"))
        entries = []
        for e in req.get("offsets") or []:
            log = self.log(e.get("topic"), e.get("partition"))
            o = e.get("offset")
            if not isinstance(o, int) or not 0 <= o <= log.end_offset:
                raise BrokerError("invalid_offset", str(o))
            entries.append((e["topic"], e["partition"], o))
        self.offsets.commit(group, entries)
        return {}

    def api_committed(self, req):
        self.log(req.get("topic"), req.get("partition"))
        return {"offset": self.offsets.get(req.get("group"), req["topic"], req["partition"])}

    # ---- networking ---------------------------------------------------------

    def serve(self, host: str = "127.0.0.1", port: int = 0) -> tuple[str, int]:
        broker = self

        class Handler(socketserver.BaseRequestHandler):
            def handle(self):
                sock: socket.socket = self.request
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                while not broker._stop.is_set():
                    try:
                        req = recv_frame(sock)
                    except (ProtocolError, OSError):
                        return
                    if req is None:
                        return
                    resp: dict
                    try:
                        resp = broker.handle(req)
                    except BrokerError as e:
                        resp = {"error": e.code, "message": str(e)}
                    except Exception as e:  # keep the connection alive; report the bug
                        resp = {"error": "internal", "message": repr(e)}
                    resp["id"] = req.get("id")
                    try:
                        send_frame(sock, resp)
                    except OSError:
                        return

        class Server(socketserver.ThreadingTCPServer):
            allow_reuse_address = True
            daemon_threads = True

        self._server = Server((host, port), Handler)
        addr = self._server.server_address[:2]
        self._spawn(self._server.serve_forever)
        self._spawn(self._sweeper)
        if self.leader:
            from .replica import Replicator

            self.replicator = Replicator(self, self.leader)
            self._spawn(self.replicator.run)
        return addr

    def _spawn(self, target) -> None:
        t = threading.Thread(target=target, daemon=True)
        t.start()
        self._threads.append(t)

    def _sweeper(self) -> None:
        while not self._stop.wait(0.5):
            self.coordinator.sweep()

    def close(self) -> None:
        self._stop.set()
        if self._server:
            self._server.shutdown()
            self._server.server_close()
        for t in self._threads:
            t.join(timeout=5)
        for log in self.logs.values():
            log.close()
        self.offsets.close()
