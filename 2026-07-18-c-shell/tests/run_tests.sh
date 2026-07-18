#!/usr/bin/env bash
# Smoke tests for myshell, run via `make test`.
set -u
cd "$(dirname "$0")/.."
SHELL_BIN=./myshell
TMP=$(mktemp -d)
fail=0

check() {
    local desc="$1" expected="$2" actual="$3"
    if [ "$expected" != "$actual" ]; then
        echo "FAIL: $desc"
        echo "  expected: $expected"
        echo "  actual:   $actual"
        fail=1
    else
        echo "ok: $desc"
    fi
}

out=$(printf 'echo hi there\nexit\n' | $SHELL_BIN 2>/dev/null | sed -n '1p')
check "echo prints its args" "hi there" "$out"

out=$(printf 'ls | grep src\nexit\n' | $SHELL_BIN 2>/dev/null | sed -n '1p')
check "pipe filters output" "src" "$out"

printf 'echo redirected > %s/redir.txt\nexit\n' "$TMP" | $SHELL_BIN >/dev/null
check "> writes a file" "redirected" "$(cat "$TMP/redir.txt")"

printf 'echo one > %s/append.txt\necho two >> %s/append.txt\nexit\n' "$TMP" "$TMP" | $SHELL_BIN >/dev/null
check ">> appends" "$(printf 'one\ntwo')" "$(cat "$TMP/append.txt")"

code=$(printf 'exit 7\n' | $SHELL_BIN >/dev/null; echo $?)
check "exit <n> sets the exit code" "7" "$code"

out=$(printf 'cd /tmp\npwd\nexit\n' | $SHELL_BIN 2>/dev/null | sed -n '1p')
check "cd changes directory" "/tmp" "$out"

rm -rf "$TMP"
if [ "$fail" -eq 0 ]; then
    echo "All tests passed."
else
    echo "Some tests FAILED."
fi
exit $fail
