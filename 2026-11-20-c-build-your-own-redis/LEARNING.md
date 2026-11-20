# Learning: Build Your Own Redis with C/C++ (in plain C, no dependencies)

**Source:** James Smith, [Build Your Own Redis with C/C++](https://build-your-own.org/redis/), from the C/C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
build-your-own.org was blocked from the build environment (the proxy returned 403). So this follows the book's
subject and structure (a length-prefixed protocol, a non-blocking event loop, a progressively-rehashed hashtable, an
AVL-backed sorted set, and timers for idle connections and TTLs), not its text. The book uses C++. This is **C11
plus POSIX only**, with no third-party code.

> Written by the daily-project routine (Claude). Commits are authored as `Claude`, so they are separate
> from the repo owner's own work in the history.

## Run it

```
make                                   # bin/redis-server, bin/redis-cli
./bin/redis-server -p 1234 -i 5000     # port, idle timeout in ms (-p 0 = any free port)

./bin/redis-cli set greeting hello     # (nil)
./bin/redis-cli get greeting           # (str) hello
./bin/redis-cli pexpire greeting 1500  # (int) 1
./bin/redis-cli pttl greeting          # (int) 1499
./bin/redis-cli zadd board 120 ann     # (int) 1
./bin/redis-cli zadd board 95 bob      # (int) 1
./bin/redis-cli zadd board 140 cy      # (int) 1
./bin/redis-cli zrank board ann        # (int) 1
./bin/redis-cli zquery board 100 '' 0 10
# (arr) len=4
# (str) ann
# (dbl) 120
# (str) cy
# (dbl) 140
# (arr) end
./bin/redis-cli get board              # (err) 3 not a string value
./bin/redis-cli get greeting           # 1.6 s later: (nil)
```

Test: `make test` builds everything with **AddressSanitizer + UndefinedBehaviorSanitizer** and runs 20 tests in about
2 seconds:

- 7 data-structure tests: the hashtable across rehashes, AVL invariants under 6,000 random inserts and deletes,
  offset and rank, heap ordering and back-references, sorted-set ordering.
- 8 command tests, with an injected clock.
- 5 end-to-end tests that start the real (sanitized) server binary on a free port and talk to it over TCP:
  two clients, pipelining, a request sent one byte at a time, a 4 MiB value, malformed input, idle timeout and
  active expiry.

Each e2e test ends by sending SIGTERM and requiring exit code 0. The server frees everything on shutdown, so
LeakSanitizer turns any leak into a failed test.

I also broke the code on purpose to check that the tests notice. Removing the AVL double rotation fails the balance
check. Dropping a `buf_free` in `conn_destroy` fails the e2e test through LeakSanitizer. Turning off lazy expiry
fails 4 TTL checks.

## Layout

| Path | What it does |
| --- | --- |
| `src/proto.c` | Wire format: request parsing (bounds-checked, rejects trailing bytes), tagged response encoding, a printer used by the CLI and tests |
| `src/buf.c` | Growable byte buffer that's cheap to consume from the front (connection in/out buffers) |
| `src/hashtable.c` | Intrusive chaining hashtable with **progressive rehashing** |
| `src/avl.c` | Intrusive AVL tree **augmented with subtree counts**, giving `avl_offset` and `avl_rank` in O(log n) |
| `src/zset.c` | Sorted set: hashtable by name and AVL tree by (score, name) over the same nodes |
| `src/heap.c` | Min-heap of expiry times whose items keep their owner's index up to date |
| `src/list.h` | Intrusive doubly-linked list, the idle-connection queue |
| `src/db.c` | Keyspace and command table. No sockets, no clock (the caller passes `now_ms`) |
| `src/server.c` | `poll()` event loop, per-connection state machine, timers, clean shutdown on SIGINT/SIGTERM |
| `src/client.c` | One-shot CLI client |

Commands: `get set del keys pexpire pttl persist zadd zrem zscore zrank zcard zquery`.
`zquery key score name offset limit` finds the first member >= (score, name), skips `offset` members (which may be
negative), and returns up to `limit` (name, score) pairs. That covers range-by-score, range-by-rank and pagination.

## What I learned

- **TCP is a byte stream, not a message stream.** One `read` can return half a request, or three and a half. The
  fix is a length prefix plus a per-connection input buffer: append whatever arrived, then
  `while (try_one_request())` takes off every *complete* request. That same loop is what makes **pipelining** work
  for free. The e2e tests check both ends: three requests in one `write`, and one request sent a byte at a time.
- **Length prefixes beat delimiters.** Every string is `[u32 len][bytes]`, so values are binary-safe and the parser
  never scans. The parser also checks every length against the bytes that are actually left. For example, it rejects
  `nstr = 0xFFFFFF` in a 4-byte body *before* allocating for it. A bad request closes only that connection, which a
  test checks while another client keeps working.
- **Non-blocking I/O means each connection is a small state machine.** A connection either wants to read or wants to
  write. After answering, the server stops reading until the replies are flushed. That's backpressure: a client that
  pipelines forever without reading can't make the server buffer unlimited output. It also tries to write right away
  instead of waiting for the next `poll()`, because the socket is almost always writable.
- **The event loop's timeout is the timer system.** `poll()` sleeps until the *nearest* deadline. That's the oldest
  idle connection (the head of a list kept in activity order, so it's O(1)) or the earliest key TTL (the top of a
  min-heap). Timers use `CLOCK_MONOTONIC`, so a wall-clock change can't expire everything at once.
- **Expiry needs two halves.** Active expiry (the heap, reaped each tick and capped at 2,000 keys so one tick stays
  short) frees memory for keys nobody reads. Lazy expiry (checked on every lookup) makes sure no command ever sees a
  key whose deadline passed but which hasn't been reaped yet. A test turns off the lazy half and watches it fail.
- **Progressive rehashing spreads a resize's cost over many requests.** When the load factor hits 8, a table twice
  the size is created, and every later lookup, insert or delete moves at most 128 nodes across. A lookup checks both
  tables in the meantime. A single-threaded server can't afford one O(n) pause, because every client waits on it.
- **Intrusive data structures let one object live in several indexes.** A sorted-set member is *one* allocation that
  holds an `HNode` (hashtable by name, for O(1) `zscore`) and an `AVLNode` (tree by score, for ordered queries).
  `container_of` gets back to the member from either one. Changing a score means unlinking from the tree,
  updating, and re-linking. The hashtable doesn't change.
- **Storing subtree sizes turns a balanced tree into an order-statistics tree.** Keeping `cnt` next to `height`
  costs one addition per rotation. In return, "the k-th node after this one" and "rank of this node" become
  O(log n) walks, instead of the O(offset) walk that would make deep pagination slow.
- **AVL deletion with two children is the tricky case.** Detach the in-order successor (it has no left child), then
  move it into the deleted node's exact place. Rebalancing while detaching may have *moved* the node being deleted,
  so read its parent and child links only after the detach.
- **A heap that owners can modify needs back-references.** Each heap item stores a pointer to its key's `heap_idx`,
  and every swap updates it. That makes `PERSIST`, re-`PEXPIRE` and `DEL` O(log n) instead of a linear search.

## How this differs from the book

- C instead of C++: intrusive structs with `container_of` instead of templates and classes.
- `poll()` (as the book starts with), not `epoll`.
- No thread pool for freeing very large sorted sets in the background. `zset_clear` runs inline.
- Added `persist`, `zrank`, `zcard` and lazy expiry. `SET` on an existing key replaces any type and drops its TTL,
  as in Redis.
- Request and response integers are explicitly little-endian, so the format doesn't depend on the host.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Based on the subject of [Build Your Own Redis with C/C++](https://build-your-own.org/redis/) by James Smith.
