"""Client library: a connection, a Producer, and a group-aware Consumer."""

from __future__ import annotations

import itertools
import socket
import threading
import time
from dataclasses import dataclass

from .protocol import BrokerError, ProtocolError, b64, recv_frame, send_frame, unb64


class Connection:
    """One TCP connection; requests are serialised with a lock (one in flight at a time)."""

    def __init__(self, host: str, port: int, timeout: float = 40.0):
        self.addr = (host, port)
        self.sock = socket.create_connection(self.addr, timeout=timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.lock = threading.Lock()
        self._ids = itertools.count(1)

    def request(self, api: str, **fields) -> dict:
        with self.lock:
            rid = next(self._ids)
            send_frame(self.sock, {"api": api, "id": rid, **fields})
            resp = recv_frame(self.sock)
        if resp is None:
            raise ProtocolError("broker closed the connection")
        if resp.get("id") != rid:
            raise ProtocolError(f"response id {resp.get('id')} != request id {rid}")
        if "error" in resp:
            raise BrokerError(resp["error"], resp.get("message", ""))
        return resp

    def create_topic(self, topic: str, partitions: int = 1) -> bool:
        return self.request("create_topic", topic=topic, partitions=partitions)["created"]

    def metadata(self) -> dict:
        return self.request("metadata")

    def close(self) -> None:
        self.sock.close()


def _to_bytes(x) -> bytes | None:
    if x is None or isinstance(x, bytes):
        return x
    return str(x).encode()


class Producer:
    def __init__(self, conn: Connection):
        self.conn = conn

    def send(self, topic: str, value, key=None, partition: int | None = None) -> tuple[int, int]:
        """Returns (partition, offset) once the broker has appended the record."""
        return self.send_batch(topic, [(key, value)], partition)[0]

    def send_batch(self, topic: str, items, partition: int | None = None) -> list[tuple[int, int]]:
        records = [{"key": b64(_to_bytes(k)), "value": b64(_to_bytes(v))} for k, v in items]
        resp = self.conn.request("produce", topic=topic, partition=partition, records=records)
        return [(r["partition"], r["offset"]) for r in resp["results"]]


@dataclass(frozen=True)
class Message:
    topic: str
    partition: int
    offset: int
    timestamp: int
    key: bytes | None
    value: bytes


class Consumer:
    """Subscribe to topics as part of a group. The broker decides which partitions this
    member owns; `poll()` reads them, `commit()` saves progress.

    Delivery is at-least-once: commit *after* processing, and anything processed but
    not yet committed is redelivered to whoever owns the partition next.
    """

    def __init__(
        self,
        conn: Connection,
        group: str,
        topics: list[str],
        auto_offset_reset: str = "earliest",
        heartbeat_interval: float = 1.0,
    ):
        if auto_offset_reset not in ("earliest", "latest"):
            raise ValueError("auto_offset_reset must be 'earliest' or 'latest'")
        self.conn = conn
        self.group = group
        self.topics = list(topics)
        self.reset = auto_offset_reset
        self.heartbeat_interval = heartbeat_interval
        self.member_id: str | None = None
        self.generation = -1
        self.assignment: list[tuple[str, int]] = []
        self.positions: dict[tuple[str, int], int] = {}
        self._last_hb = 0.0
        self._next = 0  # round-robin cursor over assigned partitions
        self.rebalances = 0
        self._join()

    def _join(self) -> None:
        resp = self.conn.request("join_group", group=self.group, topics=self.topics, member_id=self.member_id)
        self.member_id = resp["member_id"]
        self.generation = resp["generation"]
        self.assignment = [(t, p) for t, p in resp["assignment"]]
        self.positions = {tp: self._initial_position(*tp) for tp in self.assignment}
        self._committed = dict(self.positions)
        self._last_hb = time.monotonic()
        self.rebalances += 1

    def _initial_position(self, topic: str, partition: int) -> int:
        committed = self.conn.request("committed", group=self.group, topic=topic, partition=partition)["offset"]
        if committed is not None:
            return committed
        meta = self.conn.metadata()["topics"][topic][partition]
        return meta["start"] if self.reset == "earliest" else meta["end"]

    def heartbeat(self) -> None:
        """Tell the coordinator we're alive; rejoin (and pick up a new assignment) if the group moved on."""
        try:
            self.conn.request("heartbeat", group=self.group, member_id=self.member_id, generation=self.generation)
            self._last_hb = time.monotonic()
        except BrokerError as e:
            if e.code not in ("rebalance_in_progress", "unknown_member"):
                raise
            if e.code == "unknown_member":
                self.member_id = None
            self._join()

    def poll(self, timeout: float = 1.0, max_bytes: int = 1 << 20) -> list[Message]:
        """Return the next batch of messages from one of our partitions (round-robin),
        long-polling up to `timeout` seconds when nothing is available."""
        if time.monotonic() - self._last_hb >= self.heartbeat_interval:
            self.heartbeat()
        if not self.assignment:
            time.sleep(min(timeout, self.heartbeat_interval))
            return []
        deadline = time.monotonic() + timeout
        n = len(self.assignment)
        # One pass without waiting so a quiet partition can't starve a busy one...
        for i in range(n):
            tp = self.assignment[(self._next + i) % n]
            msgs = self._fetch(tp, 0, max_bytes)
            if msgs:
                self._next = (self._next + i + 1) % n
                return msgs
        # ...then park on one partition until data arrives or time runs out.
        wait = max(0, min(deadline - time.monotonic(), self.heartbeat_interval))
        tp = self.assignment[self._next % n]
        self._next = (self._next + 1) % n
        return self._fetch(tp, int(wait * 1000), max_bytes)

    def _fetch(self, tp, wait_ms: int, max_bytes: int) -> list[Message]:
        topic, partition = tp
        try:
            resp = self.conn.request(
                "fetch", topic=topic, partition=partition, offset=self.positions[tp],
                max_wait_ms=wait_ms, max_bytes=max_bytes,
            )
        except BrokerError as e:
            if e.code != "offset_out_of_range":
                raise
            # Retention deleted what we wanted (or a bogus commit): apply the reset policy.
            meta = self.conn.metadata()["topics"][topic][partition]
            self.positions[tp] = meta["start"] if self.reset == "earliest" else meta["end"]
            return []
        msgs = [
            Message(topic, partition, r["offset"], r["ts"], unb64(r["key"]), unb64(r["value"]))
            for r in resp["records"]
        ]
        if msgs:
            self.positions[tp] = msgs[-1].offset + 1
        return msgs

    def commit(self) -> bool:
        """Commit current positions. Returns False (and rejoins) if a rebalance fenced us out;
        the uncommitted messages will be redelivered to the partitions' new owners."""
        offsets = [
            {"topic": t, "partition": p, "offset": o}
            for (t, p), o in self.positions.items()
            if self._committed.get((t, p)) != o
        ]
        if not offsets:
            return True
        try:
            self.conn.request(
                "commit", group=self.group, member_id=self.member_id, generation=self.generation, offsets=offsets
            )
            self._committed.update(self.positions)
            return True
        except BrokerError as e:
            if e.code not in ("rebalance_in_progress", "unknown_member"):
                raise
            if e.code == "unknown_member":
                self.member_id = None
            self._join()
            return False

    def close(self) -> None:
        try:
            self.conn.request("leave_group", group=self.group, member_id=self.member_id)
        except (BrokerError, ProtocolError, OSError):
            pass
