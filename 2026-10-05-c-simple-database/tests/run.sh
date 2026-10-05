#!/bin/sh
# End-to-end tests: feed scripts to ./db and compare output.
set -u
cd "$(dirname "$0")/.."
DB=$(mktemp -u /tmp/dbtest.XXXXXX)
fail=0
run() { printf '%s\n' "$1" | ./db "$DB"; }
check() { # name expected actual
    if [ "$2" = "$3" ]; then echo "ok   - $1"; else echo "FAIL - $1"; echo "--- expected"; echo "$2"; echo "--- got"; echo "$3"; fail=1; fi
}
reset() { rm -f "$DB"; }

reset
out=$(run "insert 1 user1 a@x.com
select
.exit")
check "insert and select" "db > Executed.
db > (1, user1, a@x.com)
Executed.
db > " "$out"

reset
out=$(run "insert 1 a a@x
insert 1 b b@x
.exit")
check "duplicate key rejected" "db > Executed.
db > Error: Duplicate key.
db > " "$out"

reset
long=$(printf 'a%.0s' $(seq 1 33))
out=$(run "insert 1 $long e@x
insert -1 u e@x
.exit")
check "validation errors" "db > String is too long.
db > ID must be positive.
db > " "$out"

reset
run "insert 2 bob b@x
insert 1 al a@x
.exit" >/dev/null
out=$(run "select
.exit")
check "persists across restarts, sorted by key" "db > (1, al, a@x)
(2, bob, b@x)
Executed.
db > " "$out"

reset
out=$( (i=1; while [ $i -le 14 ]; do echo "insert $i u$i e$i"; i=$((i+1)); done; echo ".btree"; echo ".exit") | ./db "$DB" | sed 's/^db > //' | sed -n '/^Tree:/,$p')
check "root leaf split into internal + 2 leaves" "Tree:
- internal (size 1)
  - leaf (size 7)
    - 1
    - 2
    - 3
    - 4
    - 5
    - 6
    - 7
  - key 7
  - leaf (size 7)
    - 8
    - 9
    - 10
    - 11
    - 12
    - 13
    - 14" "$out"

reset
# 500 shuffled inserts, then verify select returns all in order and find works.
seq 1 500 | awk 'BEGIN{srand(7)}{print rand()" "$1}' | sort -n | awk '{print "insert "$2" u"$2" e"$2}' > /tmp/dbtest.in
(cat /tmp/dbtest.in; echo ".exit") | ./db "$DB" > /dev/null
got=$(printf 'select\n.exit\n' | ./db "$DB" | sed 's/^db > //' | grep -c '^(')
ids=$(printf 'select\n.exit\n' | ./db "$DB" | sed 's/^db > //' | grep '^(' | sed 's/^(\([0-9]*\),.*/\1/' | tr '\n' ' ')
want=$(seq 1 500 | tr '\n' ' ')
check "500 random inserts: all rows, sorted" "500 $want" "$got $ids"
out=$(printf 'find 250\nfind 9999\n.exit\n' | ./db "$DB")
check "find by key" "db > (250, u250, e250)
Executed.
db > Executed.
db > " "$out"
rm -f /tmp/dbtest.in "$DB"

[ $fail -eq 0 ] && echo "all tests passed"
exit $fail
