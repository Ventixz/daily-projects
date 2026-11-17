"""Append-only, segmented, crash-safe partition log.

On-disk layout for one partition directory:

    00000000000000000000.log    records, back to back
    00000000000000000000.index  sparse index: (relative offset u32, file position u32) pairs
    00000000000000001234.log    next segment, named after its first offset
    ...

Record layout (big-endian):

    length  u32   bytes that follow this field
    crc     u32   crc32 of everything after this field
    offset  u64
    ts_ms   u64
    key_len i32   -1 means "no key"
    key     bytes
    value   bytes (the rest)
"""

from __future__ import annotations

import os
import struct
import threading
import time
import zlib
from bisect import bisect_right
from dataclasses import dataclass

_LEN = struct.Struct(">I")
_HEAD = struct.Struct(">IQQi")  # crc, offset, ts, key_len
_INDEX = struct.Struct(">II")
MAX_RECORD = 16 * 1024 * 1024


@dataclass(frozen=True)
class Record:
    offset: int
    timestamp: int
    key: bytes | None
    value: bytes


def encode_record(offset: int, ts: int, key: bytes | None, value: bytes) -> bytes:
    klen = -1 if key is None else len(key)
    body = struct.pack(">QQi", offset, ts, klen) + (key or b"") + value
    crc = zlib.crc32(body)
    return _LEN.pack(4 + len(body)) + struct.pack(">I", crc) + body


def decode_records(buf: bytes):
    """Yield (Record, bytes_consumed_so_far). Stops at the first incomplete or corrupt record."""
    pos = 0
    while pos + 4 <= len(buf):
        (length,) = _LEN.unpack_from(buf, pos)
        if length < _HEAD.size or length > MAX_RECORD or pos + 4 + length > len(buf):
            return
        crc, offset, ts, klen = _HEAD.unpack_from(buf, pos + 4)
        body = buf[pos + 8 : pos + 4 + length]
        if zlib.crc32(body) != crc:
            return
        kstart = pos + 4 + _HEAD.size
        if klen < -1 or kstart + max(klen, 0) > pos + 4 + length:
            return
        key = None if klen == -1 else bytes(buf[kstart : kstart + klen])
        value = bytes(buf[kstart + max(klen, 0) : pos + 4 + length])
        pos += 4 + length
        yield Record(offset, ts, key, value), pos


class Segment:
    def __init__(self, directory: str, base_offset: int, index_interval: int):
        self.base = base_offset
        self.index_interval = index_interval
        stem = os.path.join(directory, f"{base_offset:020d}")
        self.log_path = stem + ".log"
        self.index_path = stem + ".index"
        self._log = open(self.log_path, "a+b")
        self._index_entries: list[tuple[int, int]] = []  # (relative offset, position)
        self._bytes_since_index = 0
        self.next_offset = base_offset
        self.size = 0
        self._recover()
        self._index = open(self.index_path, "ab")

    def _recover(self) -> None:
        """Scan the whole segment, keep the valid prefix, rebuild the index from it.

        The index is a cache: rebuilding it from the log is always correct, so a
        torn index write can never point readers at garbage.
        """
        self._log.seek(0)
        data = self._log.read()
        valid = 0
        since = self.index_interval  # force an index entry for the first record
        entries = []
        for rec, end in decode_records(data):
            if rec.offset != self.next_offset:
                break
            if since >= self.index_interval:
                entries.append((rec.offset - self.base, valid))
                since = 0
            since += end - valid
            valid = end
            self.next_offset = rec.offset + 1
        if valid != len(data):
            self._log.truncate(valid)  # drop a torn/corrupt tail
        self._log.seek(0, os.SEEK_END)
        self.size = valid
        self._bytes_since_index = since
        self._index_entries = entries
        with open(self.index_path, "wb") as f:
            for rel, p in entries:
                f.write(_INDEX.pack(rel, p))

    def append(self, ts: int, key: bytes | None, value: bytes) -> int:
        offset = self.next_offset
        data = encode_record(offset, ts, key, value)
        if not self._index_entries or self._bytes_since_index >= self.index_interval:
            entry = (offset - self.base, self.size)
            self._index_entries.append(entry)
            self._index.write(_INDEX.pack(*entry))
            self._bytes_since_index = 0
        self._log.write(data)
        self.size += len(data)
        self._bytes_since_index += len(data)
        self.next_offset = offset + 1
        return offset

    def flush(self, fsync: bool = False) -> None:
        self._log.flush()
        self._index.flush()
        if fsync:
            os.fsync(self._log.fileno())

    def read(self, offset: int, max_bytes: int) -> list[Record]:
        """Records with offset >= `offset`, stopping after roughly max_bytes (always at least one)."""
        rels = [rel for rel, _ in self._index_entries]
        i = bisect_right(rels, offset - self.base) - 1
        start = self._index_entries[i][1] if i >= 0 else 0
        self._log.flush()
        with open(self.log_path, "rb") as f:
            f.seek(start)
            data = f.read(self.size - start)
        out: list[Record] = []
        used = 0
        for rec, end in decode_records(data):
            if rec.offset < offset:
                continue
            out.append(rec)
            used = end
            if used >= max_bytes:
                break
        return out

    def close(self) -> None:
        self._log.close()
        self._index.close()

    def delete(self) -> None:
        self.close()
        for p in (self.log_path, self.index_path):
            if os.path.exists(p):
                os.remove(p)


class OffsetOutOfRange(Exception):
    pass


class PartitionLog:
    """One partition: an ordered list of segments, one writer, many readers."""

    def __init__(
        self,
        directory: str,
        segment_bytes: int = 1 << 20,
        index_interval: int = 4096,
        retention_bytes: int | None = None,
        fsync: bool = False,
    ):
        os.makedirs(directory, exist_ok=True)
        self.directory = directory
        self.segment_bytes = segment_bytes
        self.index_interval = index_interval
        self.retention_bytes = retention_bytes
        self.fsync = fsync
        self.lock = threading.RLock()
        self.new_data = threading.Condition(self.lock)
        bases = sorted(
            int(name[:-4]) for name in os.listdir(directory) if name.endswith(".log")
        )
        self.segments = [Segment(directory, b, index_interval) for b in bases]
        # A segment that recovered short of the next one's base means data loss
        # in the middle of the log; keep the prefix, drop what follows.
        for i in range(len(self.segments) - 1):
            if self.segments[i].next_offset != self.segments[i + 1].base:
                for s in self.segments[i + 1 :]:
                    s.delete()
                self.segments = self.segments[: i + 1]
                break
        if not self.segments:
            self.segments = [Segment(directory, 0, index_interval)]

    @property
    def start_offset(self) -> int:
        return self.segments[0].base

    @property
    def end_offset(self) -> int:
        """Offset the next appended record will get (Kafka's 'log end offset')."""
        return self.segments[-1].next_offset

    def append(self, value: bytes, key: bytes | None = None, ts: int | None = None) -> int:
        return self.append_batch([(key, value, ts)])[0]

    def append_batch(self, items) -> list[int]:
        with self.lock:
            offsets = []
            for key, value, ts in items:
                if len(value) + len(key or b"") > MAX_RECORD - 64:
                    raise ValueError("record too large")
                active = self.segments[-1]
                if active.size >= self.segment_bytes:
                    active.flush(self.fsync)
                    active = Segment(self.directory, active.next_offset, self.index_interval)
                    self.segments.append(active)
                offsets.append(active.append(ts if ts is not None else int(time.time() * 1000), key, value))
            self.segments[-1].flush(self.fsync)
            self._enforce_retention()
            self.new_data.notify_all()
            return offsets

    def append_replica(self, records: list[Record]) -> None:
        """Follower path: records must arrive with exactly the next offsets."""
        with self.lock:
            for r in records:
                if r.offset != self.end_offset:
                    raise ValueError(f"replica gap: expected {self.end_offset}, got {r.offset}")
                self.append_batch([(r.key, r.value, r.timestamp)])

    def _enforce_retention(self) -> None:
        if self.retention_bytes is None:
            return
        while len(self.segments) > 1 and sum(s.size for s in self.segments) > self.retention_bytes:
            self.segments.pop(0).delete()

    def read(self, offset: int, max_bytes: int = 1 << 20) -> list[Record]:
        with self.lock:
            if offset < self.start_offset or offset > self.end_offset:
                raise OffsetOutOfRange(
                    f"offset {offset} not in [{self.start_offset}, {self.end_offset}]"
                )
            if offset == self.end_offset:
                return []
            bases = [s.base for s in self.segments]
            i = bisect_right(bases, offset) - 1
            out: list[Record] = []
            budget = max_bytes
            for seg in self.segments[i:]:
                recs = seg.read(max(offset, seg.base), budget)
                out.extend(recs)
                budget -= sum(len(r.value) + len(r.key or b"") + 32 for r in recs)
                if budget <= 0:
                    break
            return out

    def wait_for(self, offset: int, timeout: float) -> None:
        """Block until a record at `offset` exists or the timeout passes (long-poll)."""
        deadline = time.monotonic() + timeout
        with self.lock:
            while self.end_offset <= offset:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return
                self.new_data.wait(remaining)

    def close(self) -> None:
        with self.lock:
            for s in self.segments:
                s.flush(self.fsync)
                s.close()
