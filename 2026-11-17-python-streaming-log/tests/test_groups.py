import time
import unittest

from streamlog import BrokerError, Consumer, Producer

from tests.test_broker import BrokerCase


def drain(consumers, want, timeout=5.0):
    got = {id(c): [] for c in consumers}
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and sum(map(len, got.values())) < want:
        for c in consumers:
            got[id(c)] += c.poll(timeout=0.05)
            c.commit()
    return [got[id(c)] for c in consumers]


class GroupTest(BrokerCase):
    session_timeout = 0.8

    def setUp(self):
        super().setUp()
        self.admin = self.connect()
        self.admin.create_topic("events", 4)
        self.prod = Producer(self.admin)

    def consumer(self, group="g", **kw):
        return Consumer(self.connect(), group, ["events"], heartbeat_interval=0.1, **kw)

    def test_two_members_split_partitions_and_see_everything_once(self):
        a = self.consumer()
        b = self.consumer()
        a.heartbeat()  # a learns about the rebalance b caused
        self.assertEqual(a.generation, b.generation)
        self.assertEqual(sorted(a.assignment + b.assignment), [("events", p) for p in range(4)])
        self.assertFalse(set(a.assignment) & set(b.assignment))
        self.prod.send_batch("events", [(f"k{i}", str(i)) for i in range(40)])
        ga, gb = drain([a, b], 40)
        values = sorted(int(m.value) for m in ga + gb)
        self.assertEqual(values, list(range(40)))
        self.assertTrue(ga and gb)

    def test_independent_groups_each_get_all_messages(self):
        self.prod.send_batch("events", [(None, str(i)) for i in range(12)])
        (g1,) = drain([self.consumer("billing")], 12)
        (g2,) = drain([self.consumer("audit")], 12)
        self.assertEqual(len(g1), 12)
        self.assertEqual(len(g2), 12)

    def test_crashed_member_is_expired_and_its_partitions_reassigned(self):
        a = self.consumer()
        b = self.consumer()
        a.heartbeat()
        self.assertEqual(len(a.assignment), 2)
        self.assertEqual(len(b.assignment), 2)
        # b "crashes": stops heartbeating without leaving.
        for _ in range(12):  # a keeps heartbeating for 1.2 s, longer than the 0.8 s session timeout
            time.sleep(0.1)
            a.heartbeat()
        self.assertEqual(len(a.assignment), 4)
        self.prod.send_batch("events", [(None, str(i)) for i in range(8)])
        (ga,) = drain([a], 8)
        self.assertEqual(len(ga), 8)

    def test_zombie_commit_is_fenced(self):
        a = self.consumer()
        self.prod.send_batch("events", [(None, str(i)) for i in range(8)], partition=0)
        old_gen = a.generation
        msgs = drain([a], 8)[0]
        self.assertEqual(len(msgs), 8)
        self.consumer()  # a new member bumps the generation
        with self.assertRaises(BrokerError) as e:
            self.admin.request(
                "commit", group="g", member_id=a.member_id, generation=old_gen,
                offsets=[{"topic": "events", "partition": 0, "offset": 0}],
            )
        self.assertEqual(e.exception.code, "rebalance_in_progress")
        a.positions[("events", 0)] = 3  # pretend a re-processed some, then tries to commit
        self.assertFalse(a.commit())  # refused, a rejoined instead
        self.assertEqual(a.generation, old_gen + 1)
        committed = self.admin.request("committed", group="g", topic="events", partition=0)["offset"]
        self.assertEqual(committed, 8)

    def test_uncommitted_work_is_redelivered_after_handover(self):
        a = self.consumer()
        self.prod.send_batch("events", [(None, str(i)) for i in range(6)], partition=1)
        first = []
        while len(first) < 6:
            first += a.poll(timeout=0.2)
        # Commit only the first 2 (as if a crashed after processing the rest).
        a.positions[("events", 1)] = 2
        a.commit()
        a.close()  # leaves the group
        b = self.consumer()
        again = []
        deadline = time.monotonic() + 3
        while len(again) < 4 and time.monotonic() < deadline:
            again += b.poll(timeout=0.2)
        self.assertEqual([m.offset for m in again], [2, 3, 4, 5])

    def test_latest_reset_skips_history(self):
        self.prod.send_batch("events", [(None, "old")] * 5)
        c = self.consumer("tail", auto_offset_reset="latest")
        self.assertEqual(c.poll(timeout=0.2), [])
        self.prod.send("events", "new", partition=2)
        got = drain([c], 1)[0]
        self.assertEqual([m.value for m in got], [b"new"])

    def test_heartbeat_with_wrong_member_is_rejected(self):
        with self.assertRaises(BrokerError) as e:
            self.admin.request("heartbeat", group="g", member_id="nobody", generation=0)
        self.assertEqual(e.exception.code, "unknown_member")


if __name__ == "__main__":
    unittest.main()
