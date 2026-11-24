#!/usr/bin/env python3
"""End-to-end tests: start the real binary and talk to it over TCP.

Covers what unit tests cannot: framing across TCP reads, pipelining, many
clients on one thread, idle timeouts, active expiry driven by the event loop,
interop with the stock redis-cli, and, when redis-server is installed, a
differential test that sends the same commands to real Redis and compares
the raw reply bytes.
"""
import os
import random
import shutil
import socket
import subprocess
import sys
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
BINARY = os.path.join(HERE, "..", "build", "redis-server")


def start_server(*extra):
    proc = subprocess.Popen([BINARY, "--port", "0", *extra], stdout=subprocess.PIPE, text=True)
    line = proc.stdout.readline()  # "listening on 127.0.0.1:PORT"
    if not line.startswith("listening on"):
        proc.kill()
        raise RuntimeError("server did not start: %r" % line)
    return proc, int(line.rsplit(":", 1)[1])


def stop_server(proc):
    proc.terminate()
    proc.wait(5)
    if proc.stdout:
        proc.stdout.close()


def encode(*args):
    out = b"*%d\r\n" % len(args)
    for a in args:
        a = a if isinstance(a, bytes) else str(a).encode()
        out += b"$%d\r\n%s\r\n" % (len(a), a)
    return out


class Client:
    """A minimal RESP client that also exposes the raw socket."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.buf = b""

    def close(self):
        self.sock.close()

    def _fill(self):
        chunk = self.sock.recv(65536)
        if not chunk:
            raise ConnectionError("server closed the connection")
        self.buf += chunk

    def _line(self):
        while b"\r\n" not in self.buf:
            self._fill()
        line, self.buf = self.buf.split(b"\r\n", 1)
        return line

    def read_raw(self):
        """One complete reply as raw bytes (for byte-exact comparisons)."""
        line = self._line()
        kind, rest = line[:1], line[1:]
        if kind == b"$":
            n = int(rest)
            if n < 0:
                return line + b"\r\n"
            while len(self.buf) < n + 2:
                self._fill()
            data, self.buf = self.buf[: n + 2], self.buf[n + 2:]
            return line + b"\r\n" + data
        if kind == b"*":
            out = line + b"\r\n"
            for _ in range(max(int(rest), 0)):
                out += self.read_raw()
            return out
        return line + b"\r\n"

    def read(self):
        line = self._line()
        kind, rest = line[:1], line[1:]
        if kind == b"+":
            return rest.decode()
        if kind == b"-":
            return Exception(rest.decode())
        if kind == b":":
            return int(rest)
        if kind == b"$":
            n = int(rest)
            if n < 0:
                return None
            while len(self.buf) < n + 2:
                self._fill()
            data, self.buf = self.buf[:n], self.buf[n + 2:]
            return data
        if kind == b"*":
            return [self.read() for _ in range(int(rest))]
        raise ValueError("bad reply %r" % line)

    def cmd(self, *args):
        self.sock.sendall(encode(*args))
        return self.read()

    def closed_by_server(self, wait=2.0):
        self.sock.settimeout(wait)
        try:
            return self.sock.recv(1) == b""
        except (ConnectionResetError, socket.timeout):
            return False
        finally:
            self.sock.settimeout(5)


class ServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.proc, cls.port = start_server()

    @classmethod
    def tearDownClass(cls):
        stop_server(cls.proc)

    def setUp(self):
        self.c = Client(self.port)
        self.c.cmd("FLUSHALL")

    def tearDown(self):
        self.c.close()

    def test_basic_commands(self):
        c = self.c
        self.assertEqual(c.cmd("PING"), "PONG")
        self.assertEqual(c.cmd("SET", "greeting", "hello"), "OK")
        self.assertEqual(c.cmd("GET", "greeting"), b"hello")
        self.assertEqual(c.cmd("DEL", "greeting", "nope"), 1)
        self.assertIsNone(c.cmd("GET", "greeting"))

    def test_binary_safe_values(self):
        value = bytes(range(256)) * 10 + b"\r\n$3\r\n"
        self.assertEqual(self.c.cmd("SET", b"k\x00\xff", value), "OK")
        self.assertEqual(self.c.cmd("GET", b"k\x00\xff"), value)

    def test_large_value(self):
        value = os.urandom(8 * 1024 * 1024)  # far bigger than one read() or socket buffer
        self.assertEqual(self.c.cmd("SET", "big", value), "OK")
        self.assertEqual(self.c.cmd("GET", "big"), value)

    def test_pipelining(self):
        n = 20000
        payload = b"".join(encode("SET", "k%d" % i, "v%d" % i) for i in range(n))
        payload += b"".join(encode("GET", "k%d" % i) for i in range(n))
        self.c.sock.sendall(payload)
        for _ in range(n):
            self.assertEqual(self.c.read(), "OK")
        for i in range(n):
            self.assertEqual(self.c.read(), b"v%d" % i)
        self.assertEqual(self.c.cmd("DBSIZE"), n)

    def test_request_split_into_single_bytes(self):
        req = encode("SET", "slow", "drip") + encode("GET", "slow")
        for b in req:
            self.c.sock.sendall(bytes([b]))
            time.sleep(0.0005)
        self.assertEqual(self.c.read(), "OK")
        self.assertEqual(self.c.read(), b"drip")

    def test_inline_commands(self):
        self.c.sock.sendall(b"SET inline works\r\n\r\nGET inline\n")
        self.assertEqual(self.c.read(), "OK")
        self.assertEqual(self.c.read(), b"works")

    def test_protocol_error_closes_connection(self):
        self.c.sock.sendall(b"*1\r\n:oops\r\n")
        reply = self.c.read()
        self.assertIsInstance(reply, Exception)
        self.assertIn("Protocol error", str(reply))
        self.assertTrue(self.c.closed_by_server())

    def test_quit(self):
        self.assertEqual(self.c.cmd("QUIT"), "OK")
        self.assertTrue(self.c.closed_by_server())

    def test_many_concurrent_clients(self):
        clients = [Client(self.port) for _ in range(200)]
        try:
            for i, cl in enumerate(clients):  # all send before any reads
                cl.sock.sendall(encode("SET", "c%d" % i, i) + encode("GET", "c%d" % i))
            for i, cl in enumerate(clients):
                self.assertEqual(cl.read(), "OK")
                self.assertEqual(cl.read(), str(i).encode())
        finally:
            for cl in clients:
                cl.close()

    def test_client_disappearing_mid_reply(self):
        self.c.cmd("SET", "big", "x" * (4 * 1024 * 1024))
        rude = Client(self.port)
        rude.sock.sendall(encode("GET", "big") * 10)
        rude.close()  # never reads; server must not crash (SIGPIPE/EPIPE)
        time.sleep(0.2)
        self.assertEqual(self.c.cmd("PING"), "PONG")

    def test_ttl_expires_without_access(self):
        c = self.c
        c.cmd("SET", "short", "v", "PX", "150")
        c.cmd("SET", "long", "v")
        self.assertEqual(c.cmd("DBSIZE"), 2)
        time.sleep(0.4)
        # DBSIZE doesn't touch keys, so only the event loop's timer can have
        # removed it.
        self.assertEqual(c.cmd("DBSIZE"), 1)
        self.assertIsNone(c.cmd("GET", "short"))

    def test_ttl_commands(self):
        c = self.c
        c.cmd("SET", "k", "v")
        self.assertEqual(c.cmd("EXPIRE", "k", "100"), 1)
        self.assertIn(c.cmd("TTL", "k"), (99, 100))
        self.assertTrue(99000 < c.cmd("PTTL", "k") <= 100000)
        self.assertEqual(c.cmd("PERSIST", "k"), 1)
        self.assertEqual(c.cmd("TTL", "k"), -1)

    def test_sorted_set_leaderboard(self):
        c = self.c
        rng = random.Random(1)
        players = {"p%d" % i: rng.randint(0, 10000) for i in range(5000)}
        args = []
        for name, score in players.items():
            args += [score, name]
        self.assertEqual(c.cmd("ZADD", "board", *args), len(players))
        ranked = sorted(players.items(), key=lambda kv: (kv[1], kv[0]))
        top = c.cmd("ZRANGE", "board", -3, -1, "WITHSCORES")
        expect = []
        for name, score in ranked[-3:]:
            expect += [name.encode(), str(score).encode()]
        self.assertEqual(top, expect)
        for name in ("p0", "p17", "p4999"):
            self.assertEqual(c.cmd("ZRANK", "board", name), ranked.index((name, players[name])))
        page = c.cmd("ZRANGEBYSCORE", "board", "1000", "2000", "LIMIT", "10", "5")
        in_range = [n for n, s in ranked if 1000 <= s <= 2000]
        self.assertEqual(page, [n.encode() for n in in_range[10:15]])

    def test_large_zset_delete_does_not_stall(self):
        c = self.c
        n = 200000
        for start in range(0, n, 10000):
            args = []
            for i in range(start, start + 10000):
                args += [i, "m%d" % i]
            c.cmd("ZADD", "huge", *args)
        self.assertEqual(c.cmd("ZCARD", "huge"), n)
        self.assertEqual(c.cmd("DEL", "huge"), 1)  # freed on a worker thread
        self.assertEqual(c.cmd("EXISTS", "huge"), 0)


class IdleTimeoutTest(unittest.TestCase):
    def test_idle_connection_is_closed(self):
        proc, port = start_server("--idle-timeout-ms", "300")
        try:
            idle = Client(port)
            busy = Client(port)
            deadline = time.time() + 0.8
            while time.time() < deadline:  # keep one connection active
                self.assertEqual(busy.cmd("PING"), "PONG")
                time.sleep(0.05)
            self.assertTrue(idle.closed_by_server(0.5))
            self.assertEqual(busy.cmd("PING"), "PONG")
            idle.close()
            busy.close()
        finally:
            stop_server(proc)


@unittest.skipUnless(shutil.which("redis-cli"), "redis-cli not installed")
class RedisCliInteropTest(unittest.TestCase):
    def test_stock_redis_cli(self):
        proc, port = start_server()
        try:
            def cli(*args):
                return subprocess.run(["redis-cli", "-p", str(port), *args], capture_output=True,
                                      text=True, timeout=10).stdout.strip()

            self.assertEqual(cli("PING"), "PONG")
            self.assertEqual(cli("SET", "from-cli", "yes"), "OK")
            self.assertEqual(cli("GET", "from-cli"), "yes")
            self.assertEqual(cli("ZADD", "z", "1", "a", "2", "b"), "2")
            self.assertEqual(cli("ZRANGE", "z", "0", "-1", "WITHSCORES"), "a\n1\nb\n2")
        finally:
            stop_server(proc)


@unittest.skipUnless(shutil.which("redis-server"), "redis-server not installed")
class DifferentialTest(unittest.TestCase):
    """Same commands to real Redis and to ours; replies must match byte for byte."""

    @classmethod
    def setUpClass(cls):
        cls.ours, cls.our_port = start_server()
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            cls.ref_port = s.getsockname()[1]
        cls.ref = subprocess.Popen(
            ["redis-server", "--port", str(cls.ref_port), "--save", "", "--appendonly", "no"],
            stdout=subprocess.DEVNULL)
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", cls.ref_port), timeout=1).close()
                break
            except OSError:
                time.sleep(0.05)

    @classmethod
    def tearDownClass(cls):
        for p in (cls.ours, cls.ref):
            stop_server(p)

    def compare(self, commands):
        a, b = Client(self.our_port), Client(self.ref_port)
        try:
            for client in (a, b):
                client.cmd("FLUSHALL")
            for cmd in commands:
                a.sock.sendall(encode(*cmd))
                b.sock.sendall(encode(*cmd))
                self.assertEqual(a.read_raw(), b.read_raw(), "reply differs for %r" % (cmd,))
        finally:
            a.close()
            b.close()

    def test_scripted_edge_cases(self):
        self.compare([
            ("PING",), ("PING", "hi"), ("ECHO", "x"),
            ("GET", "missing"), ("SET", "k", "v"), ("GET", "k"),
            ("SET", "k", "v2", "NX"), ("SET", "k", "v3", "XX"), ("GET", "k"),
            ("SET", "n", "1", "XX"), ("SET", "k", "v", "EX", "0"), ("SET", "k", "v", "EX", "abc"),
            ("SET", "k", "v", "FOO"), ("SET", "k", "v", "NX", "XX"),
            ("EXISTS", "k", "k", "nope"), ("TYPE", "k"), ("TYPE", "nope"),
            ("TTL", "k"), ("TTL", "nope"), ("EXPIRE", "k", "100"), ("TTL", "k"),
            ("PERSIST", "k"), ("PERSIST", "k"), ("EXPIRE", "nope", "5"), ("EXPIRE", "k", "x"),
            ("EXPIRE", "k", "0"), ("EXISTS", "k"),
            ("GET",), ("SET", "only"), ("NOSUCHCMD", "a", "b"),
            ("ZADD", "z", "1", "a", "2", "b", "2", "aa", "-inf", "low", "+inf", "high"),
            ("ZADD", "z", "1.5", "a"), ("ZADD", "z", "x", "a"), ("ZADD", "z", "1", "a", "2"),
            ("ZCARD", "z"), ("ZCARD", "nope"), ("ZSCORE", "z", "a"), ("ZSCORE", "z", "high"),
            ("ZSCORE", "z", "nope"), ("ZRANK", "z", "aa"), ("ZRANK", "z", "nope"),
            ("ZRANGE", "z", "0", "-1"), ("ZRANGE", "z", "0", "-1", "WITHSCORES"),
            ("ZRANGE", "z", "-100", "100"), ("ZRANGE", "z", "3", "1"), ("ZRANGE", "z", "a", "1"),
            ("ZRANGE", "nope", "0", "-1"),
            ("ZRANGEBYSCORE", "z", "-inf", "+inf", "WITHSCORES"),
            ("ZRANGEBYSCORE", "z", "(1.5", "2"), ("ZRANGEBYSCORE", "z", "1.5", "(2"),
            ("ZRANGEBYSCORE", "z", "-inf", "+inf", "LIMIT", "1", "2"),
            ("ZRANGEBYSCORE", "z", "-inf", "+inf", "LIMIT", "2", "-1"),
            ("ZRANGEBYSCORE", "z", "x", "1"),
            ("ZINCRBY", "z", "0.25", "a"), ("ZINCRBY", "z", "1", "new"),
            ("GET", "z"), ("SET", "s", "str"), ("ZADD", "s", "1", "a"), ("ZSCORE", "s", "a"),
            ("ZREM", "z", "a", "b", "nope"), ("ZREM", "nope", "a"),
            ("ZREM", "z", "aa", "low", "high", "new"), ("EXISTS", "z"),
            ("DEL", "s", "k", "nope"), ("DBSIZE",),
        ])

    def test_random_sorted_set_workload(self):
        rng = random.Random(2024)
        cmds = []
        members = ["m%d" % i for i in range(60)]
        for _ in range(3000):
            r = rng.random()
            m = rng.choice(members)
            score = rng.choice([rng.randint(-50, 50), rng.randint(-50, 50) / 4])  # exact in binary
            if r < 0.35:
                cmds.append(("ZADD", "z", score, m))
            elif r < 0.45:
                cmds.append(("ZREM", "z", m))
            elif r < 0.55:
                cmds.append(("ZRANK", "z", m))
            elif r < 0.65:
                cmds.append(("ZSCORE", "z", m))
            elif r < 0.8:
                cmds.append(("ZRANGE", "z", rng.randint(-70, 70), rng.randint(-70, 70),
                             *(["WITHSCORES"] if rng.random() < 0.5 else [])))
            elif r < 0.95:
                lo, hi = sorted([rng.randint(-60, 60), rng.randint(-60, 60)])
                lo = ("(" if rng.random() < 0.3 else "") + str(lo)
                hi = ("(" if rng.random() < 0.3 else "") + str(hi)
                extra = ["LIMIT", rng.randint(0, 10), rng.randint(-1, 10)] if rng.random() < 0.4 else []
                cmds.append(("ZRANGEBYSCORE", "z", lo, hi, *extra))
            else:
                cmds.append(("ZCARD", "z"))
        self.compare(cmds)


if __name__ == "__main__":
    if not os.path.exists(BINARY):
        sys.exit("build first: make")
    unittest.main(verbosity=2)
