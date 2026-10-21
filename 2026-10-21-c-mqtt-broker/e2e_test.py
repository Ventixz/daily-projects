"""End-to-end test: start the broker on a free port and talk MQTT 3.1.1 to it over raw sockets."""
import socket, struct, subprocess, sys


def varint(n):
    out = bytearray()
    while True:
        d, n = n % 128, n // 128
        out.append(d | 128 if n else d)
        if not n:
            return bytes(out)


def s(text):
    b = text.encode()
    return struct.pack(">H", len(b)) + b


def packet(first, body=b""):
    return bytes([first]) + varint(len(body)) + body


class Client:
    def __init__(self, port, cid, keepalive=60):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=2)
        self.buf = b""
        self.send(packet(0x10, s("MQTT") + bytes([4, 2]) + struct.pack(">H", keepalive) + s(cid)))
        assert self.read() == (0x20, b"\x00\x00"), "CONNACK"

    def send(self, data):
        self.sock.sendall(data)

    def read(self, timeout=2):
        """Return (first byte, body) of the next packet, or None on timeout / closed."""
        self.sock.settimeout(timeout)
        while True:
            if len(self.buf) >= 2:
                n, mult, i = 0, 1, 1
                while True:
                    n += (self.buf[i] & 127) * mult
                    if not self.buf[i] & 128:
                        break
                    mult *= 128
                    i += 1
                if len(self.buf) >= i + 1 + n:
                    pkt = (self.buf[0], self.buf[i + 1:i + 1 + n])
                    self.buf = self.buf[i + 1 + n:]
                    return pkt
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                return None
            except ConnectionError:
                return None
            if not chunk:
                return None
            self.buf += chunk

    def subscribe(self, pid, *filters):
        self.send(packet(0x82, struct.pack(">H", pid) + b"".join(s(f) + b"\x00" for f in filters)))
        return self.read()

    def publish(self, topic, payload, qos=0, pid=1):
        body = s(topic) + (struct.pack(">H", pid) if qos else b"") + payload
        self.send(packet(0x30 | qos << 1, body))


def main():
    broker = subprocess.Popen([sys.argv[1], "0"], stdout=subprocess.PIPE, text=True)
    failures = 0

    def check(name, cond):
        nonlocal failures
        print(("ok   " if cond else "FAIL ") + name)
        failures += not cond

    try:
        port = int(broker.stdout.readline().split()[-1])
        a, b, c, pub = (Client(port, n) for n in ("a", "b", "c", "pub"))

        check("suback grants qos0", a.subscribe(1, "sensors/+/temp") == (0x90, b"\x00\x01\x00"))
        b.subscribe(1, "sensors/#")
        check("invalid filter rejected (0x80)", c.subscribe(1, "other", "bad#filter") == (0x90, b"\x00\x01\x00\x80"))

        pub.publish("sensors/kitchen/temp", b"21.5")
        want = (0x30, s("sensors/kitchen/temp") + b"21.5")
        check("+ subscriber gets message", a.read() == want)
        check("# subscriber gets message", b.read() == want)
        check("unrelated subscriber gets nothing", c.read(0.2) is None)

        pub.publish("sensors/kitchen/humidity", b"40")
        check("+ filter ignores other leaf", a.read(0.2) is None)
        check("# filter still matches", b.read()[1].endswith(b"40"))

        pub.publish("sensors/x/temp", b"1", qos=1, pid=0x0102)
        check("qos1 publish is acked", pub.read() == (0x40, b"\x01\x02"))
        check("qos1 delivered at qos0", a.read() == (0x30, s("sensors/x/temp") + b"1"))
        b.read()

        a.send(packet(0xA2, struct.pack(">H", 5) + s("sensors/+/temp")))
        check("unsuback", a.read() == (0xB0, b"\x00\x05"))
        pub.publish("sensors/kitchen/temp", b"22")
        check("no delivery after unsubscribe", a.read(0.2) is None)
        b.read()

        a.send(packet(0xC0))
        check("pingresp", a.read() == (0xD0, b""))

        # Overlapping filters on one client still deliver once.
        b.subscribe(2, "sensors/kitchen/#")
        pub.publish("sensors/kitchen/temp", b"23")
        b.read()
        check("overlapping filters deliver once", b.read(0.2) is None)

        # Session takeover: reconnecting with the same client id kicks the old connection.
        c2 = Client(port, "c")
        check("old session closed on takeover", c.read(0.5) is None)
        c2.subscribe(1, "other")

        # Protocol violations close the connection.
        raw = socket.create_connection(("127.0.0.1", port), timeout=2)
        raw.sendall(packet(0xC0))  # PINGREQ before CONNECT
        check("packet before CONNECT drops connection", raw.recv(10) == b"")
        v3 = socket.create_connection(("127.0.0.1", port), timeout=2)
        v3.sendall(packet(0x10, s("MQIsdp") + bytes([3, 2, 0, 60]) + s("old")))
        check("unsupported protocol gets CONNACK 0x01", v3.recv(10) == bytes([0x20, 2, 0, 1]))
        bad = Client(port, "bad")
        bad.send(packet(0x30 | 6, s("t") + b"x"))  # QoS 3
        check("malformed publish drops connection", bad.read(1) is None)

        # Large payload crosses TCP read boundaries.
        big = b"z" * 30000
        pub.publish("sensors/big/temp", big)
        got = b.read()
        check("large payload intact", got is not None and got[1].endswith(big))
    finally:
        broker.kill()
    print("failures:", failures)
    return failures != 0


if __name__ == "__main__":
    sys.exit(main())
