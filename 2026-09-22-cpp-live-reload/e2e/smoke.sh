#!/usr/bin/env bash
# Builds the real host + plugin, runs the host in the background, then
# rebuilds the plugin mid-run (from a scratch copy -- the tracked
# plugin/plugin.cpp is never touched) and checks that the host picked up
# the new build without restarting and without losing simulated state.
set -euo pipefail
cd "$(dirname "$0")/.."

# Build directly rather than via `make build`: a prior e2e run leaves
# build/plugin.so containing the v2 variant, with an mtime newer than the
# tracked plugin/plugin.cpp, so make's staleness check would (wrongly)
# consider it already up to date and skip rebuilding it from source.
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude -o build/host src/host.cpp src/reloader.cpp -ldl
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude -fPIC -shared -o build/plugin.so plugin/plugin.cpp

WORKDIR=$(mktemp -d)
HOST_PID=""

cleanup() {
    if [ -n "$HOST_PID" ]; then
        kill "$HOST_PID" 2>/dev/null || true
        wait "$HOST_PID" 2>/dev/null || true
    fi
    rm -rf "$WORKDIR"
}
trap cleanup EXIT

LOG="$WORKDIR/host.log"
# 60 ticks, 50ms apart -> ~3s of wall-clock run time to swap the plugin in.
./build/host build/plugin.so 60 0.05 50 > "$LOG" 2>&1 &
HOST_PID=$!

sleep 1
if ! grep -q 'label=gravity-v1' "$LOG"; then
    echo "FAIL: never observed gravity-v1 running"
    cat "$LOG"
    exit 1
fi

sed -e 's/restitution = 0.6/restitution = 0.9/' \
    -e 's/"gravity-v1"/"gravity-v2"/' \
    plugin/plugin.cpp > "$WORKDIR/plugin_v2.cpp"
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude -fPIC -shared -o build/plugin.so "$WORKDIR/plugin_v2.cpp"

wait "$HOST_PID" || true
HOST_PID=""

if ! grep -q 'reloaded plugin -> gravity-v2' "$LOG"; then
    echo "FAIL: host never logged a reload to gravity-v2"
    cat "$LOG"
    exit 1
fi

if ! grep -q 'label=gravity-v2' "$LOG"; then
    echo "FAIL: never observed gravity-v2 running after reload"
    cat "$LOG"
    exit 1
fi

prev=-1
for t in $(grep -oP 'tick=\K[0-9]+' "$LOG"); do
    if [ "$prev" -ge 0 ] && [ "$t" -ne $((prev + 1)) ]; then
        echo "FAIL: tick sequence not contiguous across reload ($prev -> $t) -- state was reset"
        exit 1
    fi
    prev=$t
done

echo "PASS: plugin hot-reloaded mid-run, $prev ticks simulated with state carried across the swap"
