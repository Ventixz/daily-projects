#!/usr/bin/env bash
# Feeds scripted input into the built shell binary and checks its stdout.
# No test framework - just diff-the-output, matching the rest of this repo's
# language-specific reference tests.
set -u
cd "$(dirname "$0")/.."

SHELL_BIN=./shell
PASS=0
FAIL=0
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

check() {
    local name="$1"
    local input="$2"
    local expected="$3"
    local actual
    actual=$(printf '%s' "$input" | "$SHELL_BIN" 2>&1)
    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
        echo "FAIL: $name"
        echo "  expected: $(printf '%q' "$expected")"
        echo "  actual:   $(printf '%q' "$actual")"
    fi
}

check "builtin pwd matches real pwd" \
    "pwd
exit
" \
    "$(pwd)"

check "external command via execvp" \
    "echo hello world
exit
" \
    "hello world"

check "quoted argument keeps its spaces as one token" \
    "echo 'a b' c
exit
" \
    "a b c"

check "cd changes directory seen by later commands" \
    "cd /tmp
pwd
exit
" \
    "/tmp"

check "unknown command reports command not found" \
    "totally_not_a_real_command_xyz
exit
" \
    "shell: totally_not_a_real_command_xyz: command not found"

check "output redirection with >" \
    "echo redirected > $TMPDIR/out.txt
cat $TMPDIR/out.txt
exit
" \
    "redirected"

check "append redirection with >>" \
    "echo one > $TMPDIR/append.txt
echo two >> $TMPDIR/append.txt
cat $TMPDIR/append.txt
exit
" \
    "one
two"

check "append truncates first only, not on every write" \
    "echo one > $TMPDIR/trunc.txt
echo two > $TMPDIR/trunc.txt
cat $TMPDIR/trunc.txt
exit
" \
    "two"

check "input redirection with <" \
    "echo from_file > $TMPDIR/in.txt
cat < $TMPDIR/in.txt
exit
" \
    "from_file"

check "two-stage pipeline" \
    "printf 'a\nb\nc\n' | grep b
exit
" \
    "b"

check "three-stage pipeline" \
    "printf 'apple\nbanana\navocado\n' | grep a | grep -v banana
exit
" \
    "apple
avocado"

check "builtin output ordering interleaves correctly with external output" \
    "pwd
echo marker
exit
" \
    "$(pwd)
marker"

echo ""
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
