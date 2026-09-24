#!/usr/bin/env python3
"""Protocol correctness, run against all three server binaries in turn.

Deliberately exercises TCP's "it's a byte stream" reality: sends a command
one byte at a time with a short pause, and sends two commands glued
together in a single send(). All three servers share the same linebuf_t
code, so they must all behave identically here regardless of how many
threads or epoll events it took to get the bytes there.
"""
import socket
import subprocess
import sys
import time

BASE_PORT = 9300
SERVERS = ["./sequential", "./threaded", "./epoll"]


def connect(port, timeout=2.0):
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    s.settimeout(timeout)
    return s


def recv_line(sock, buf=b""):
    while b"\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            break
        buf += chunk
    line, _, rest = buf.partition(b"\n")
    return line.decode(), rest


def run_checks(port):
    failures = []

    def check(cond, msg):
        status = "PASS" if cond else "FAIL"
        print(f"{status}: {msg}")
        if not cond:
            failures.append(msg)

    s = connect(port)
    rest = b""
    banner, rest = recv_line(s, rest)
    check(banner == "READY", "server sends READY banner on connect")

    s.sendall(b"ECHO hello world\n")
    line, rest = recv_line(s, rest)
    check(line == "hello world", "ECHO returns its argument")

    s.sendall(b"REV abcde\n")
    line, rest = recv_line(s, rest)
    check(line == "edcba", "REV reverses its argument")

    s.sendall(b"UPPER shout\n")
    line, rest = recv_line(s, rest)
    check(line == "SHOUT", "UPPER uppercases its argument")

    s.sendall(b"NONSENSE\n")
    line, rest = recv_line(s, rest)
    check(line.startswith("ERR"), "unrecognized command gets an ERR")

    # One line trickled in a byte at a time -- the classic "TCP doesn't
    # preserve message boundaries" trap.
    for byte in b"ECHO trickled\n":
        s.sendall(bytes([byte]))
        time.sleep(0.005)
    line, rest = recv_line(s, rest)
    check(line == "trickled", "a line sent one byte at a time is still reassembled correctly")

    # Two commands glued into a single send() -- the opposite trap.
    s.sendall(b"ECHO first\nECHO second\n")
    line1, rest = recv_line(s, rest)
    line2, rest = recv_line(s, rest)
    check(line1 == "first" and line2 == "second", "two commands in one packet are both answered, in order")

    s.sendall(b"QUIT\n")
    line, rest = recv_line(s, rest)
    check(line == "BYE", "QUIT replies BYE")
    s.settimeout(1.0)
    remainder = s.recv(16)
    check(remainder == b"", "connection is closed after BYE")

    s.close()
    return failures


def main():
    all_failures = []
    for i, server in enumerate(SERVERS):
        port = BASE_PORT + i
        print(f"\n=== {server} on port {port} ===")
        proc = subprocess.Popen([server, str(port)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            time.sleep(0.3)
            failures = run_checks(port)
            all_failures.extend(f"{server}: {f}" for f in failures)
        finally:
            proc.terminate()
            proc.wait(timeout=2)

    if all_failures:
        print(f"\n{len(all_failures)} check(s) FAILED:")
        for f in all_failures:
            print(f"  - {f}")
        sys.exit(1)
    print("\nAll correctness checks passed on all three servers")


if __name__ == "__main__":
    main()
