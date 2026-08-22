#!/usr/bin/env bash
# Runs the whole pipeline against a local fake peer: generate a .torrent for
# resources/demo.txt, seed it, download it with gotorrent's -peer bypass,
# and diff the result against the original file.
set -euo pipefail
cd "$(dirname "$0")/.."

PORT=6881
OUT="$(mktemp -d)/demo.out.txt"

./bin/gentorrent -in resources/demo.txt -out resources/demo.torrent -piece-length 32768
./bin/demoseeder -file resources/demo.txt -port "$PORT" &
seeder_pid=$!
trap 'kill "$seeder_pid" 2>/dev/null || true' EXIT

sleep 0.3
./bin/gotorrent -torrent resources/demo.torrent -out "$OUT" -peer "127.0.0.1:$PORT"

if diff -q resources/demo.txt "$OUT" >/dev/null; then
	echo "OK: downloaded file is byte-identical to resources/demo.txt"
else
	echo "MISMATCH: downloaded file differs from resources/demo.txt" >&2
	exit 1
fi
