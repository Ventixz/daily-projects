import os
import tempfile
import threading
import unittest

from streamlog.log import OffsetOutOfRange, PartitionLog, decode_records, encode_record


class RecordFormatTest(unittest.TestCase):
    def test_roundtrip_with_and_without_key(self):
        buf = encode_record(5, 1000, b"k", b"hello") + encode_record(6, 1001, None, b"")
        recs = [r for r, _ in decode_records(buf)]
        self.assertEqual([(r.offset, r.key, r.value) for r in recs], [(5, b"k", b"hello"), (6, None, b"")])

    def test_corrupt_byte_stops_decoding(self):
        good = encode_record(0, 1, None, b"aaaa")
        bad = bytearray(encode_record(1, 1, None, b"bbbb"))
        bad[-1] ^= 0xFF  # flip a value byte; crc no longer matches
        recs = [r for r, _ in decode_records(good + bytes(bad) + encode_record(2, 1, None, b"c"))]
        self.assertEqual([r.offset for r in recs], [0])

    def test_truncated_tail_is_ignored(self):
        buf = encode_record(0, 1, None, b"x") + encode_record(1, 1, None, b"yyyy")[:-2]
        self.assertEqual([r.offset for r, _ in decode_records(buf)], [0])


class PartitionLogTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = os.path.join(self.tmp.name, "t-0")

    def tearDown(self):
        self.tmp.cleanup()

    def test_offsets_are_dense_and_reads_start_anywhere(self):
        log = PartitionLog(self.dir)
        offs = [log.append(f"m{i}".encode()) for i in range(10)]
        self.assertEqual(offs, list(range(10)))
        self.assertEqual([r.value for r in log.read(7)], [b"m7", b"m8", b"m9"])
        self.assertEqual(log.read(10), [])  # at the end: empty, not an error
        with self.assertRaises(OffsetOutOfRange):
            log.read(11)
        log.close()

    def test_rolls_segments_and_reads_across_them(self):
        log = PartitionLog(self.dir, segment_bytes=200, index_interval=64)
        for i in range(50):
            log.append(f"value-{i:03d}".encode(), key=b"k")
        self.assertGreater(len(log.segments), 3)
        bases = [s.base for s in log.segments]
        self.assertEqual(bases, sorted(bases))
        got = log.read(0, max_bytes=1 << 20)
        self.assertEqual([r.offset for r in got], list(range(50)))
        # Start in the middle of a later segment.
        self.assertEqual(log.read(37, 1 << 20)[0].value, b"value-037")
        log.close()

    def test_max_bytes_limits_but_always_returns_one(self):
        log = PartitionLog(self.dir)
        for i in range(20):
            log.append(b"x" * 100)
        self.assertLess(len(log.read(0, max_bytes=300)), 20)
        self.assertEqual(len(log.read(0, max_bytes=1)), 1)
        log.close()

    def test_reopen_recovers_state(self):
        log = PartitionLog(self.dir, segment_bytes=150)
        for i in range(30):
            log.append(str(i).encode())
        log.close()
        log = PartitionLog(self.dir, segment_bytes=150)
        self.assertEqual(log.end_offset, 30)
        self.assertEqual(log.append(b"next"), 30)
        self.assertEqual([r.value for r in log.read(28)], [b"28", b"29", b"next"])
        log.close()

    def test_torn_write_is_truncated_on_recovery(self):
        log = PartitionLog(self.dir)
        for i in range(5):
            log.append(f"r{i}".encode())
        path = log.segments[-1].log_path
        log.close()
        # Simulate a crash halfway through writing record 5.
        with open(path, "ab") as f:
            f.write(encode_record(5, 1, None, b"half-written")[:9])
        size_with_garbage = os.path.getsize(path)
        log = PartitionLog(self.dir)
        self.assertEqual(log.end_offset, 5)
        self.assertLess(os.path.getsize(path), size_with_garbage)
        self.assertEqual(log.append(b"r5"), 5)
        self.assertEqual(log.read(5)[0].value, b"r5")
        log.close()

    def test_corrupt_index_is_rebuilt_from_log(self):
        log = PartitionLog(self.dir, index_interval=1)
        for i in range(20):
            log.append(f"v{i}".encode())
        idx = log.segments[0].index_path
        log.close()
        with open(idx, "wb") as f:
            f.write(b"\xff" * 64)  # nonsense positions
        log = PartitionLog(self.dir, index_interval=1)
        self.assertEqual([r.value for r in log.read(12)][:2], [b"v12", b"v13"])
        log.close()

    def test_retention_deletes_oldest_segments(self):
        log = PartitionLog(self.dir, segment_bytes=200, retention_bytes=600)
        for i in range(100):
            log.append(f"value-{i:03d}".encode())
        self.assertGreater(log.start_offset, 0)
        self.assertEqual(log.end_offset, 100)
        with self.assertRaises(OffsetOutOfRange):
            log.read(0)
        self.assertEqual(log.read(log.start_offset)[0].offset, log.start_offset)
        files = os.listdir(self.dir)
        self.assertEqual(len([f for f in files if f.endswith(".log")]), len(log.segments))
        log.close()

    def test_concurrent_appends_get_unique_offsets(self):
        log = PartitionLog(self.dir, segment_bytes=4096)
        results = []
        lock = threading.Lock()

        def worker(n):
            mine = [log.append(f"{n}-{i}".encode()) for i in range(200)]
            with lock:
                results.extend(mine)

        threads = [threading.Thread(target=worker, args=(n,)) for n in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(sorted(results), list(range(1600)))
        self.assertEqual(len(log.read(0, 1 << 30)), 1600)
        log.close()

    def test_wait_for_wakes_on_append(self):
        log = PartitionLog(self.dir)
        threading.Timer(0.1, lambda: log.append(b"late")).start()
        import time

        t0 = time.monotonic()
        log.wait_for(0, timeout=5)
        self.assertLess(time.monotonic() - t0, 2)
        self.assertEqual(log.end_offset, 1)
        log.close()


if __name__ == "__main__":
    unittest.main()
