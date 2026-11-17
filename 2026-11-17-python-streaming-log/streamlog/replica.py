"""Follower replication: a follower is just a consumer of the leader's partitions that
appends what it reads to its own log at the *same offsets*."""

from __future__ import annotations

import time

from .client import Connection
from .log import Record
from .protocol import BrokerError, ProtocolError, unb64


class Replicator:
    def __init__(self, broker, leader: tuple[str, int], idle_sleep: float = 0.05):
        self.broker = broker
        self.leader = leader
        self.idle_sleep = idle_sleep
        self.conn: Connection | None = None
        self.errors = 0

    def run(self) -> None:
        stop = self.broker._stop
        while not stop.is_set():
            try:
                if self.conn is None:
                    self.conn = Connection(*self.leader, timeout=10)
                copied = self.step()
                if not copied:
                    stop.wait(self.idle_sleep)
            except (OSError, ProtocolError, BrokerError):
                self.errors += 1
                if self.conn:
                    self.conn.close()
                self.conn = None
                stop.wait(0.5)  # leader down: retry later

    def step(self) -> int:
        """One pass over every leader partition. Returns how many records were copied."""
        meta = self.conn.metadata()["topics"]
        copied = 0
        for topic, parts in meta.items():
            if not self.broker.has_topic(topic):
                self.broker.create_topic(topic, len(parts))
            for info in parts:
                p = info["partition"]
                local = self.broker.log(topic, p)
                if local.end_offset < info["start"] or local.end_offset > info["end"]:
                    # Our copy fell behind the leader's retention, or is ahead of a leader
                    # that lost data: start over from the leader's first offset.
                    local = self.broker.reset_partition(topic, p, info["start"])
                if local.end_offset == info["end"]:
                    continue
                resp = self.conn.request("fetch", topic=topic, partition=p, offset=local.end_offset)
                recs = [Record(r["offset"], r["ts"], unb64(r["key"]), unb64(r["value"])) for r in resp["records"]]
                local.append_replica(recs)
                copied += len(recs)
        return copied

    def wait_caught_up(self, timeout: float = 5.0) -> bool:
        """Test helper: True once every local partition matches the leader's end offset."""
        deadline = time.monotonic() + timeout
        check = Connection(*self.leader)
        try:
            while time.monotonic() < deadline:
                meta = check.metadata()["topics"]
                if all(
                    self.broker.has_topic(t)
                    and all(self.broker.log(t, i["partition"]).end_offset == i["end"] for i in parts)
                    for t, parts in meta.items()
                ):
                    return True
                time.sleep(0.02)
            return False
        finally:
            check.close()
