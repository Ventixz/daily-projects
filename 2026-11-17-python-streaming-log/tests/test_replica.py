import os
import tempfile
import unittest

from streamlog import Broker, BrokerError, Connection, Producer


class ReplicationTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.leader = Broker(os.path.join(self.tmp.name, "leader"), segment_bytes=300)
        self.leader_addr = self.leader.serve()
        self.follower = None
        self.conns = []

    def start_follower(self):
        self.follower = Broker(os.path.join(self.tmp.name, "follower"), segment_bytes=300, leader=self.leader_addr)
        self.follower_addr = self.follower.serve()

    def conn(self, addr):
        c = Connection(*addr)
        self.conns.append(c)
        return c

    def tearDown(self):
        for c in self.conns:
            c.close()
        if self.follower:
            self.follower.close()
        self.leader.close()
        self.tmp.cleanup()

    def test_follower_copies_topics_with_identical_offsets(self):
        lc = self.conn(self.leader_addr)
        lc.create_topic("a", 3)
        lc.create_topic("b", 1)
        prod = Producer(lc)
        prod.send_batch("a", [(f"k{i}", f"v{i}") for i in range(60)])
        self.start_follower()
        prod.send_batch("b", [(None, "after-follower-started")])
        self.assertTrue(self.follower.replicator.wait_caught_up())
        fc = self.conn(self.follower_addr)
        self.assertEqual(lc.metadata()["topics"], fc.metadata()["topics"])
        for p in range(3):
            lead = lc.request("fetch", topic="a", partition=p, offset=0, max_bytes=1 << 20)["records"]
            foll = fc.request("fetch", topic="a", partition=p, offset=0, max_bytes=1 << 20)["records"]
            self.assertEqual(lead, foll)  # same offsets, keys, values and timestamps

    def test_follower_is_read_only(self):
        self.start_follower()
        fc = self.conn(self.follower_addr)
        self.assertEqual(fc.metadata()["role"], "follower")
        for call in (lambda: fc.create_topic("x", 1), lambda: Producer(fc).send("x", "v")):
            with self.assertRaises(BrokerError) as e:
                call()
            self.assertEqual(e.exception.code, "not_leader")

    def test_follower_resumes_after_restart(self):
        lc = self.conn(self.leader_addr)
        lc.create_topic("t", 1)
        Producer(lc).send_batch("t", [(None, f"m{i}") for i in range(20)])
        self.start_follower()
        self.assertTrue(self.follower.replicator.wait_caught_up())
        self.follower.close()
        Producer(lc).send_batch("t", [(None, f"n{i}") for i in range(20)])
        self.start_follower()
        self.assertTrue(self.follower.replicator.wait_caught_up())
        self.assertEqual(self.follower.log("t", 0).end_offset, 40)
        self.assertEqual(self.follower.log("t", 0).read(39)[0].value, b"n19")

    def test_follower_restarts_partition_when_behind_leader_retention(self):
        self.leader.close()
        self.leader = Broker(os.path.join(self.tmp.name, "leader2"), segment_bytes=200, retention_bytes=500)
        self.leader_addr = self.leader.serve()
        lc = self.conn(self.leader_addr)
        lc.create_topic("t", 1)
        Producer(lc).send_batch("t", [(None, f"value-{i:03d}") for i in range(100)])
        start = self.leader.log("t", 0).start_offset
        self.assertGreater(start, 0)
        self.start_follower()
        self.assertTrue(self.follower.replicator.wait_caught_up())
        flog = self.follower.log("t", 0)
        self.assertEqual((flog.start_offset, flog.end_offset), (start, 100))
        self.assertEqual(flog.read(start)[0].value, f"value-{start:03d}".encode())


if __name__ == "__main__":
    unittest.main()
