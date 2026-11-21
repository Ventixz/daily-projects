#!/bin/sh
# End-to-end tests: compile each tests/programs/*.pl0 to C, build it with a
# C compiler (warnings are errors, so the generated code has to be clean),
# run it, and compare stdout, stderr and exit status with the expectations:
#   NAME.out   expected stdout (required)
#   NAME.in    stdin (optional)
#   NAME.code  expected exit status (optional, default 0)
#   NAME.err   expected stderr (optional, default empty)
# Then check that each tests/errors/*.pl0 is rejected with the message written
# in its first line:  { error: line N: message }
set -u
cd "$(dirname "$0")/.."
CC=${CC:-cc}
BUILD=build/tests
mkdir -p "$BUILD"
pass=0
fail=0

bad() {
    echo "FAIL $1: $2"
    fail=$((fail + 1))
}

for src in tests/programs/*.pl0; do
    name=$(basename "$src" .pl0)
    dir=tests/programs
    if ! ./pl0c -o "$BUILD/$name.c" "$src" 2> "$BUILD/$name.pl0c.err"; then
        bad "$name" "pl0c failed: $(cat "$BUILD/$name.pl0c.err")"
        continue
    fi
    if ! $CC -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -o "$BUILD/$name" "$BUILD/$name.c" 2> "$BUILD/$name.cc.err"; then
        bad "$name" "C compiler rejected generated code:"
        cat "$BUILD/$name.cc.err"
        continue
    fi
    input=/dev/null
    [ -f "$dir/$name.in" ] && input="$dir/$name.in"
    "$BUILD/$name" < "$input" > "$BUILD/$name.stdout" 2> "$BUILD/$name.stderr"
    code=$?
    want_code=0
    [ -f "$dir/$name.code" ] && want_code=$(cat "$dir/$name.code")
    if [ "$code" != "$want_code" ]; then
        bad "$name" "exit status $code, expected $want_code"
        continue
    fi
    if ! cmp -s "$BUILD/$name.stdout" "$dir/$name.out"; then
        bad "$name" "stdout differs:"
        diff "$dir/$name.out" "$BUILD/$name.stdout" | head -20
        continue
    fi
    want_err=/dev/null
    [ -f "$dir/$name.err" ] && want_err="$dir/$name.err"
    if ! cmp -s "$BUILD/$name.stderr" "$want_err"; then
        bad "$name" "stderr differs:"
        diff "$want_err" "$BUILD/$name.stderr" | head -20
        continue
    fi
    pass=$((pass + 1))
done

for src in tests/errors/*.pl0; do
    name=$(basename "$src" .pl0)
    want=$(sed -n '1s/^{ error: \(.*\) }$/\1/p' "$src")
    if [ -z "$want" ]; then
        bad "error/$name" "first line must be: { error: line N: message }"
        continue
    fi
    if ./pl0c "$src" > /dev/null 2> "$BUILD/err.$name"; then
        bad "error/$name" "compiled, but should have failed with: $want"
        continue
    fi
    got=$(cat "$BUILD/err.$name")
    expected="pl0c: $src: error: $want"
    if [ "$got" != "$expected" ]; then
        bad "error/$name" "wrong message
  want: $expected
  got:  $got"
        continue
    fi
    pass=$((pass + 1))
done

echo "end-to-end: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
