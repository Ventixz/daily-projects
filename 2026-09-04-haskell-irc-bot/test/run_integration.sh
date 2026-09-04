#!/usr/bin/env bash
# End-to-end test: runs the real bot binary against the scripted mock
# server binary over an actual TCP socket on 127.0.0.1, and checks both
# processes exit cleanly. This is the only place the socket code in
# app/Main.hs gets exercised — everything in test/Spec.hs is pure and
# never opens a socket at all.
set -uo pipefail

BOT_BIN="${1:-bin/ircbot}"
MOCK_BIN="${2:-bin/mock-server}"
PORT="${PORT:-17667}"

"$MOCK_BIN" "$PORT" > /tmp/mock-server.out 2> /tmp/mock-server.err &
MOCK_PID=$!

cleanup() {
  kill "$MOCK_PID" >/dev/null 2>&1 || true
  kill "${BOT_PID:-}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

# Wait for the mock server to actually bind, by watching its own log line
# rather than opening a throwaway probe connection: this server does a
# single 'accept' for the whole test, so a probe connection would be the
# one it accepts, starving the real bot's connection.
for _ in $(seq 1 50); do
  grep -q "listening on $PORT" /tmp/mock-server.err 2>/dev/null && break
  sleep 0.1
done

"$BOT_BIN" --host 127.0.0.1 --port "$PORT" --nick hs-daily-bot --channel '#test' --max-retries 0 \
  > /tmp/bot.out 2> /tmp/bot.err &
BOT_PID=$!

wait "$MOCK_PID"
MOCK_STATUS=$?

if [ "$MOCK_STATUS" -ne 0 ]; then
  echo "mock server exited with status $MOCK_STATUS" >&2
  echo "--- mock server stderr ---" >&2
  cat /tmp/mock-server.err >&2
  echo "--- bot stderr ---" >&2
  cat /tmp/bot.err >&2
  exit 1
fi

if ! grep -q MOCK_SERVER_OK /tmp/mock-server.out; then
  echo "mock server did not report success" >&2
  cat /tmp/mock-server.out >&2
  exit 1
fi

# The bot's own socket read hits EOF once the mock server closes, and it
# should exit on its own; give it a moment, then make sure it's gone
# instead of stuck or crash-looping.
sleep 0.3
if kill -0 "$BOT_PID" >/dev/null 2>&1; then
  echo "bot process did not exit after the mock server closed the connection" >&2
  cat /tmp/bot.err >&2
  exit 1
fi
wait "$BOT_PID" 2>/dev/null
BOT_STATUS=$?
if [ "$BOT_STATUS" -ne 0 ]; then
  echo "bot exited with status $BOT_STATUS" >&2
  cat /tmp/bot.err >&2
  exit 1
fi

echo "integration test passed: full session transcript below"
echo "--- bot stderr (wire trace) ---"
cat /tmp/bot.err
