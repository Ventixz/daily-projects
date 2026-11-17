"""Command line: run a broker, create topics, produce from stdin, consume as a group."""

from __future__ import annotations

import argparse
import json
import signal
import sys
import threading

from .broker import Broker
from .client import Connection, Consumer, Producer


def hostport(s: str) -> tuple[str, int]:
    host, _, port = s.rpartition(":")
    return host or "127.0.0.1", int(port)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python -m streamlog")
    sub = ap.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("broker", help="run a broker")
    b.add_argument("--data", required=True, help="data directory")
    b.add_argument("--listen", default="127.0.0.1:9092", type=hostport)
    b.add_argument("--follow", type=hostport, help="replicate this leader (read-only follower)")
    b.add_argument("--segment-bytes", type=int, default=1 << 20)
    b.add_argument("--retention-bytes", type=int)

    for name in ("create", "produce", "consume", "metadata"):
        p = sub.add_parser(name)
        p.add_argument("--broker", default="127.0.0.1:9092", type=hostport)
        if name in ("create", "produce"):
            p.add_argument("topic")
        if name == "create":
            p.add_argument("--partitions", type=int, default=3)
        if name == "produce":
            p.add_argument("--keyed", action="store_true", help="stdin lines are KEY<TAB>VALUE")
        if name == "consume":
            p.add_argument("topics", nargs="+")
            p.add_argument("--group", required=True)
            p.add_argument("--from", dest="reset", choices=["earliest", "latest"], default="earliest")

    args = ap.parse_args(argv)

    if args.cmd == "broker":
        broker = Broker(args.data, segment_bytes=args.segment_bytes,
                        retention_bytes=args.retention_bytes, leader=args.follow)
        host, port = broker.serve(*args.listen)
        role = f"follower of {args.follow[0]}:{args.follow[1]}" if args.follow else "leader"
        print(f"streamlog broker ({role}) on {host}:{port}, data in {args.data}", flush=True)
        done = threading.Event()
        signal.signal(signal.SIGINT, lambda *_: done.set())
        signal.signal(signal.SIGTERM, lambda *_: done.set())
        done.wait()
        broker.close()
        return 0

    conn = Connection(*args.broker)
    try:
        if args.cmd == "create":
            created = conn.create_topic(args.topic, args.partitions)
            print(f"{'created' if created else 'exists'}: {args.topic} ({args.partitions} partitions)")
        elif args.cmd == "metadata":
            print(json.dumps(conn.metadata(), indent=2))
        elif args.cmd == "produce":
            prod = Producer(conn)
            for line in sys.stdin:
                line = line.rstrip("\n")
                key, value = line.split("\t", 1) if args.keyed and "\t" in line else (None, line)
                p, off = prod.send(args.topic, value, key=key)
                print(f"{args.topic}/{p}@{off}", file=sys.stderr)
        elif args.cmd == "consume":
            c = Consumer(conn, args.group, args.topics, auto_offset_reset=args.reset)
            print(f"member {c.member_id}, partitions {c.assignment}", file=sys.stderr)
            try:
                while True:
                    before = c.rebalances
                    for m in c.poll(timeout=1.0):
                        key = m.key.decode(errors="replace") if m.key is not None else "-"
                        print(f"{m.topic}/{m.partition}@{m.offset} {key} {m.value.decode(errors='replace')}", flush=True)
                    c.commit()
                    if c.rebalances != before:
                        print(f"rebalanced: partitions {c.assignment}", file=sys.stderr)
            except KeyboardInterrupt:
                c.commit()
                c.close()
    finally:
        conn.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
