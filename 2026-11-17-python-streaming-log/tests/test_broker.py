import socket
import struct
import tempfile
import threading
import time
import unittest

from streamlog import Broker, BrokerError, Connection, Consumer, Producer
from streamlog.broker import partition_for, range_assign
from streamlog.protocol import recv_frame


class BrokerCase(unittest.TestCase):
    session_timeout = 10.0

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.broker = self.start_broker()
        self.conns = []

    def start_broker(self, **kw):
        b = Broker(self.tmp.name, session_timeout=self.session_timeout, **kw)
        self.addr = b.serve()
        return b

    def connect(self):
        c = Connection(*self.addr)
        self.conns.append(c)
        return c

    def tearDown(self):
        for c in self.conns:
            c.close()
        self.broker.close()
        self.tmp.cleanup()


class AssignmentTest(unittest.TestCase):
    def test_range_assign_splits_contiguously(self):
        a = range_assign(["m1", "m2", "m3"], {m: ["t"] for m in ["m1", "m2", "m3"]}, {"t": 7})
        self.assertEqual(a["m1"], [("t", 0), ("t", 1), ("t", 2)])
        self.assertEqual(a["m2"], [("t", 3), ("t", 4)])
        self.assertEqual(a["m3"], [("t", 5), ("t", 6)])

    def test_range_assign_respects_subscriptions_and_extra_members(self):
        subs = {"a": ["x", "y"], "b": ["y"], "c": ["y"]}
        a = range_assign(["a", "b", "c"], subs, {"x": 2, "y": 2})
        self.assertEqual(a["a"], [("x", 0), ("x", 1), ("y", 0)])
        self.assertEqual(a["b"], [("y", 1)])
        self.assertEqual(a["c"], [])  # more members than partitions: someone idles

    def test_partitioner_is_stable(self):
        self.assertEqual(partition_for(b"user-42", 6), partition_for(b"user-42", 6))


class ProduceFetchTest(BrokerCase):
    def test_same_key_same_partition_in_order(self):
        c = self.connect()
        c.create_topic("orders", 4)
        prod = Producer(c)
        results = [prod.send("orders", f"event-{i}", key=f"user-{i % 3}") for i in range(30)]
        by_key = {}
        for i, (p, off) in enumerate(results):
            by_key.setdefault(f"user-{i % 3}", set()).add(p)
        self.assertTrue(all(len(ps) == 1 for ps in by_key.values()))
        p = results[0][0]
        recs = c.request("fetch", topic="orders", partition=p, offset=0)["records"]
        from streamlog.protocol import unb64

        values = [unb64(r["value"]).decode() for r in recs if unb64(r["key"]) == b"user-0"]
        self.assertEqual(values, [f"event-{i}" for i in range(0, 30, 3)])

    def test_unkeyed_records_spread_round_robin(self):
        c = self.connect()
        c.create_topic("logs", 3)
        parts = [p for p, _ in Producer(c).send_batch("logs", [(None, str(i)) for i in range(9)])]
        self.assertEqual(sorted(set(parts)), [0, 1, 2])

    def test_errors_are_reported_not_fatal(self):
        c = self.connect()
        with self.assertRaises(BrokerError) as e:
            Producer(c).send("missing", "x")
        self.assertEqual(e.exception.code, "unknown_topic")
        for bad in ("", "../etc", "__internal", "a" * 101, "sp ace"):
            with self.assertRaises(BrokerError):
                c.create_topic(bad, 1)
        c.create_topic("t", 2)
        with self.assertRaises(BrokerError) as e:
            c.create_topic("t", 5)
        self.assertEqual(e.exception.code, "topic_exists")
        self.assertFalse(c.create_topic("t", 2))  # idempotent with the same count
        with self.assertRaises(BrokerError) as e:
            c.request("fetch", topic="t", partition=0, offset=99)
        self.assertEqual(e.exception.code, "offset_out_of_range")
        with self.assertRaises(BrokerError):
            c.request("fetch", topic="t", partition=7, offset=0)
        with self.assertRaises(BrokerError):
            c.request("bogus")
        # Same connection still works after all of that.
        self.assertEqual(Producer(c).send("t", "ok", partition=1), (1, 0))

    def test_fetch_long_polls_until_data_arrives(self):
        c, c2 = self.connect(), self.connect()
        c.create_topic("t", 1)
        threading.Timer(0.2, lambda: Producer(c2).send("t", "wake")).start()
        t0 = time.monotonic()
        resp = c.request("fetch", topic="t", partition=0, offset=0, max_wait_ms=5000)
        elapsed = time.monotonic() - t0
        self.assertEqual(len(resp["records"]), 1)
        self.assertGreater(elapsed, 0.1)
        self.assertLess(elapsed, 3)

    def test_fetch_wait_times_out_empty(self):
        c = self.connect()
        c.create_topic("t", 1)
        t0 = time.monotonic()
        resp = c.request("fetch", topic="t", partition=0, offset=0, max_wait_ms=150)
        self.assertEqual(resp["records"], [])
        self.assertGreaterEqual(time.monotonic() - t0, 0.14)

    def test_binary_values_survive(self):
        c = self.connect()
        c.create_topic("bin", 1)
        blob = bytes(range(256)) * 4
        Producer(c).send("bin", blob, key=b"\x00\xff")
        cons = Consumer(c, "g", ["bin"])
        msgs = cons.poll(timeout=1)
        self.assertEqual((msgs[0].key, msgs[0].value), (b"\x00\xff", blob))

    def test_oversized_frame_drops_connection_only(self):
        s = socket.create_connection(self.addr)
        s.sendall(struct.pack(">I", 200 * 1024 * 1024))
        s.settimeout(2)
        self.assertEqual(s.recv(1), b"")  # broker hung up on us
        s.close()
        self.assertIn("role", self.connect().metadata())  # and is still serving others

    def test_garbage_json_drops_connection(self):
        s = socket.create_connection(self.addr)
        s.sendall(struct.pack(">I", 5) + b"{nope")
        s.settimeout(2)
        self.assertIsNone(recv_frame(s))
        s.close()

    def test_data_and_offsets_survive_restart(self):
        c = self.connect()
        c.create_topic("t", 2)
        Producer(c).send_batch("t", [(None, f"m{i}") for i in range(10)])
        cons = Consumer(c, "g", ["t"])
        got = []
        while len(got) < 6:
            got += cons.poll(timeout=0.5)
        cons.commit()
        c.close()
        self.conns.remove(c)
        self.broker.close()

        self.broker = self.start_broker()
        c = self.connect()
        meta = c.metadata()["topics"]["t"]
        self.assertEqual(sum(p["end"] for p in meta), 10)
        cons = Consumer(c, "g", ["t"])
        rest = []
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and len(got) + len(rest) < 10:
            rest += cons.poll(timeout=0.2)
        seen = {(m.partition, m.offset) for m in got + rest}
        self.assertEqual(len(seen), 10)  # nothing lost, and the committed ones weren't redelivered
        self.assertEqual(len(got) + len(rest), 10)


if __name__ == "__main__":
    unittest.main()
