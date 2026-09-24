#!/usr/bin/env python3
"""The actual point of this project, measured rather than just asserted.

Client A connects, sends SLEEP 800, waits for OK, then QUITs -- all on a
background thread, driven independently. As soon as A's SLEEP is in
flight, client B connects and sends ECHO fast on the main thread, and we
time how long B waits for its reply.

  - sequential: the accept loop doesn't call accept() again until A's
    *entire* connection ends (SLEEP, then QUIT), so B isn't even accepted
    until then -- B's reply should take roughly as long as A's SLEEP.
  - threaded / epoll: B should be answered almost immediately, regardless
    of what A is doing.
"""
import socket
import subprocess
import sys
import threading
import time

BASE_PORT = 9400
SLEEP_MS = 800
FAST_THRESHOLD_S = 0.3          # threaded/epoll must beat this
SLOW_LOWER_BOUND_S = SLEEP_MS / 1000 * 0.8  # sequential must be at least this slow


def recv_line(sock, buf=b""):
    while b"\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            break
        buf += chunk
    line, _, rest = buf.partition(b"\n")
    return line.decode(), rest


def drive_client_a(port, sleep_ms, ready_event):
    a = socket.create_connection(("127.0.0.1", port), timeout=5)
    buf = b""
    recv_line(a, buf)  # READY
    a.sendall(f"SLEEP {sleep_ms}\n".encode())
    ready_event.set()  # B may start now that A's SLEEP is in flight
    line, buf = recv_line(a, buf)
    assert line == "OK", f"expected 'OK' from A, got {line!r}"
    a.sendall(b"QUIT\n")
    recv_line(a, buf)  # BYE
    a.close()


def measure_b_latency(port):
    ready_event = threading.Event()
    a_thread = threading.Thread(target=drive_client_a, args=(port, SLEEP_MS, ready_event))
    a_thread.start()
    if not ready_event.wait(timeout=5):
        raise RuntimeError("client A never got far enough to signal readiness")

    start = time.monotonic()
    b = socket.create_connection(("127.0.0.1", port), timeout=5)
    b_buf = b""
    recv_line(b, b_buf)  # READY
    b.sendall(b"ECHO fast\n")
    line, b_buf = recv_line(b, b_buf)
    elapsed = time.monotonic() - start
    assert line == "fast", f"expected 'fast', got {line!r}"
    b.close()

    a_thread.join(timeout=5)
    return elapsed


def main():
    servers = [
        ("./sequential", BASE_PORT, lambda t: t >= SLOW_LOWER_BOUND_S,
         f">= {SLOW_LOWER_BOUND_S:.2f}s (B isn't accepted until A's whole connection ends)"),
        ("./threaded", BASE_PORT + 1, lambda t: t < FAST_THRESHOLD_S,
         f"< {FAST_THRESHOLD_S:.2f}s (A's SLEEP only blocks A's thread)"),
        ("./epoll", BASE_PORT + 2, lambda t: t < FAST_THRESHOLD_S,
         f"< {FAST_THRESHOLD_S:.2f}s (A's SLEEP is a timerfd, not a blocking call)"),
    ]

    failures = []
    for binary, port, predicate, expectation in servers:
        proc = subprocess.Popen([binary, str(port)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            time.sleep(0.3)
            elapsed = measure_b_latency(port)
            ok = predicate(elapsed)
            status = "PASS" if ok else "FAIL"
            print(f"{status}: {binary} -- B answered in {elapsed:.3f}s, expected {expectation}")
            if not ok:
                failures.append(binary)
        finally:
            proc.terminate()
            proc.wait(timeout=2)

    if failures:
        print(f"\n{len(failures)} server(s) FAILED the concurrency expectation: {failures}")
        sys.exit(1)
    print("\nConcurrency model differences confirmed by measurement on all three servers")


if __name__ == "__main__":
    main()
