# Learning: Build Your Own Redis (in C++)

**Source:** James Smith, *Build Your Own Redis with C/C++* (build-your-own.org/redis). It comes from the
C/C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
build-your-own.org was blocked from the build environment, so this follows the book's **chapter list, not
its text**: sockets, protocol, event loop, key-value commands, hash table, data serialization, AVL tree,
sorted set, timers, TTL, and thread pool. The one big change is that this server speaks **real RESP**
instead of the book's own binary protocol. That way the stock `redis-cli` and `redis-benchmark` work
against it, and the tests can compare its replies byte for byte with a real Redis.

> Written by the daily-project routine (Claude), not by hand.

## What it is

A single-threaded, in-memory Redis-compatible server in about 1,600 lines of C++17 (not counting comments and blank lines), with no dependencies
beyond the standard library and POSIX:

- **Event loop:** non-blocking sockets and `poll()`, one thread serving every client. It handles
  pipelining, requests split across any number of TCP reads, and backpressure for clients that send but
  never read.
- **Hash table:** intrusive chaining with **progressive rehashing**. A resize moves at most 128 buckets
  per operation, so no single request pays for copying the whole keyspace.
- **Sorted sets:** a hash table for lookup by name, plus an **AVL tree with subtree counts** for order.
  Rank, "the k-th member", and "skip N" are O(log n).
- **Timers:** idle connections are closed through an O(1) linked list. Key TTLs use a **min-heap** whose
  items know their own positions. Both feed `poll()`'s timeout, so timers fire on time without a timer
  thread.
- **Thread pool:** deleting a sorted set with more than 10,000 members hands the freeing to a worker
  thread, so a `DEL` of a huge set doesn't freeze every other client.

Commands: `PING ECHO QUIT GET SET [EX|PX|NX|XX|KEEPTTL] DEL EXISTS TYPE KEYS DBSIZE FLUSHALL FLUSHDB EXPIRE
PEXPIRE TTL PTTL PERSIST ZADD ZINCRBY ZREM ZSCORE ZCARD ZRANK ZRANGE [WITHSCORES] ZRANGEBYSCORE [WITHSCORES]
[LIMIT]`, plus the book's `ZQUERY key score name offset limit`.

## Run it

```
make                          # build/redis-server and build/unit-tests (g++ or clang++, C++17)
./build/redis-server          # listens on 127.0.0.1:6380
redis-cli -p 6380             # the stock client works
make test                     # 16 unit tests, then 18 end-to-end tests
make sanitize                 # the same tests under AddressSanitizer + UBSan
```

Options: `--port N` (0 picks a free port and prints it), `--bind ADDR`, `--idle-timeout-ms N` (default 5
minutes), `--threads N` (default 4).

It needs Linux or macOS. On Windows 11, use WSL (`wsl --install`, then `sudo apt install build-essential
python3 redis-tools` inside it). The code uses POSIX sockets and `poll()`, which native Windows doesn't
provide in the same form.

**Tests (34):**

- **16 unit tests** (C++, no framework, about 85,000 checks). The hash table, AVL tree, sorted set and heap
  are each run through thousands of random operations and compared with `std::unordered_map`,
  `std::multiset` or `std::map`. After every step the AVL test re-checks parent links, heights, counts and
  balance. One test shows that a resize really spreads over more than 10 later operations. The others
  cover every prefix of a RESP request (all must be "incomplete", never an error), malformed frames,
  shortest double formatting, TTL edge cases, capped active expiry, glob patterns, and freeing a large set
  on the pool.
- **18 end-to-end tests** (Python stdlib) start the real binary:
  - 20,000 pipelined `SET`s followed by 20,000 `GET`s in one `send`.
  - A request dripped **one byte at a time**.
  - 8 MB binary-safe values.
  - 200 clients that all send before any of them reads.
  - A client that disconnects in the middle of a 40 MB reply (the server must survive `EPIPE`).
  - A key that expires while nobody touches it (only the event loop's timer can remove it).
  - Idle-timeout eviction that spares the busy connection.
  - Protocol errors and `QUIT`.
  - A 5,000-player leaderboard and a 200,000-member set deleted on the worker thread.
  - Interop with the stock `redis-cli`.
- **The differential tests are the strongest check.** When `redis-server` is installed, the same commands
  go to **real Redis 7.0** and to this server, and the **raw reply bytes must be identical**. That covers
  65 scripted edge cases (wrong types, bad floats, out-of-range indices, `NX`+`XX`, error text) and
  a random workload of 3,000 sorted-set operations. Both pass.

Separately, ThreadSanitizer is clean on the unit tests.

A quick `redis-benchmark` on the build machine (50 clients, loopback) gave about 140–160k requests/s
unpipelined and about 2.4M/s with `-P 16`. Real Redis on the same machine was in the same range. That is
**not** a fair race: Redis keeps statistics, notifies watchers, picks a compact encoding for each value and
much more. It mainly shows that one thread with `poll()` is not the bottleneck at this scale.

## Layout

```
src/
  server.cpp       event loop: poll(), accept, per-connection read/write state machine, timers
  resp.h/.cpp      incremental RESP parser (multibulk + inline) and reply writer
  store.h/.cpp     keyspace, lazy + active expiry, every command, glob matching
  hashtable.h/.cpp intrusive hash table with progressive rehashing
  avl.h/.cpp       intrusive AVL tree with subtree counts (rank / offset / at)
  zset.h/.cpp      sorted set = hash table + AVL tree sharing one node
  heap.h/.cpp      TTL min-heap with back-references
  list.h           intrusive doubly linked list (idle timers)
  thread_pool.*    worker pool for freeing big containers
tests/
  unit_tests.cpp   data-structure and command tests
  e2e_test.py      socket-level, redis-cli interop, and differential tests against real Redis
```

## What I learned

- **Intrusive data structures turn "who owns the node" upside down.** The hash table, tree, list and heap
  never allocate anything. The *user's* struct carries the links. In C people use `container_of()` to get
  from the embedded link back to the struct. In C++ it's cleaner to **inherit the hooks**:
  `struct ZNode : AVLNode, HNode`. Then one allocation sits in both indexes, and `static_cast<ZNode*>` from
  either hook is legal and checked by the compiler. `offsetof` only works on standard-layout types, and a
  struct holding `std::string` isn't one. So where a hook can't be a base class (the heap's back-reference,
  the connection's list node), the code stores an owner pointer or measures the offset from a real object.
- **Progressive rehashing trades a little memory for flat latency.** During a resize there are two tables.
  New keys go into the new one. Lookups check both. Every operation also moves a bounded number of buckets
  (128, and empty buckets count too, or a sparse table could make one call scan for a long time). A normal
  table would stop and copy 10 million keys at once, and every client would see that pause.
- **Augmenting a balanced tree with subtree sizes is a cheap superpower.** Storing `cnt` in each node costs
  one more line in the update function. In return, `ZRANK` walks up from the node adding left-subtree
  sizes, `ZRANGE 1000000 1000010` jumps straight to rank one million, and `ZRANGEBYSCORE ... LIMIT
  50000 10` skips 50,000 items in O(log n) instead of walking them. Rotations must update the lower node
  first, then the new subtree root.
- **AVL deletion with two children is easiest as "remove the successor, then swap it in".** The successor
  has no left child, so removing it is the easy case. Then it takes over the deleted node's links,
  height and count with one struct copy, and its new parent and children are re-pointed at it.
- **The event loop's job is to never block, and to know what to wait for.** Each connection either wants to
  read or wants to write. While replies are queued, it stops reading, which is natural backpressure. Two
  optimisations matter more than they look. First, *answer every complete request in the buffer* before
  going back to `poll()`: that is what makes pipelining fast. Second, *try writing right away* instead of
  waiting for `POLLOUT`, since the reply usually fits in the socket buffer. Consuming the input buffer with
  one `erase` per batch, not one per request, keeps 20,000 pipelined requests O(n), not O(n²).
- **TCP is a byte stream, so the parser has to be restartable.** A request can arrive in any number of
  pieces. The parser checks the whole frame first and returns "incomplete" without consuming anything. It
  only copies the arguments out once the frame is complete. A unit test feeds it every proper prefix of a
  request, and an e2e test sends one byte at a time. It also doesn't trust the header: `*1048576\r\n` is
  10 bytes, so `reserve()` is capped instead of taken from the client.
- **Timers belong in the `poll()` timeout.** The timeout is "until the nearest deadline": the oldest idle
  connection or the smallest TTL. Every connection has the same idle timeout, so moving a connection to the
  tail of a list on each activity keeps the list sorted, and the head is always next. That makes a heap
  unnecessary for idle timers. TTLs differ per key, so they need the heap. Each heap item stores a pointer
  to the key's own index field, so `PERSIST` or a new `EXPIRE` updates its heap slot in O(log n) without
  searching.
- **Expiry needs both a lazy and an active side.** *Lazy:* any lookup of an expired key deletes it and
  answers "missing", so clients never see a dead key. *Active:* the loop also removes up to 2,000 expired
  keys per iteration. The cap keeps a million keys expiring at once from stalling everyone. Without the
  active side, keys nobody reads would use memory forever. `DBSIZE` doesn't touch keys, which is how the
  e2e test shows the timer did the work.
- **Threads are for work that nobody else can see.** The server stays single-threaded, so the keyspace
  needs no locks. A huge sorted set is first unlinked from the keyspace and the heap on the main thread.
  After that, nothing can reach it, so a worker can free it with no synchronisation beyond the job queue.
  Running the unit tests under ThreadSanitizer confirms the pool itself is race-free.
- **Differential testing against the real thing finds what you didn't think to test.** Matching Redis byte
  for byte forced exact behaviour: `ZRANGE` clamping, `(` exclusive bounds, `EXPIRE k 0` deleting the key,
  an emptied sorted set disappearing, the precise error text. One difference is left on purpose: this
  server formats scores in shortest round-trip form (`0.1`), like Redis 7.2+. Redis 7.0 prints
  `0.10000000000000001`, so the random test uses scores that are exact in binary (multiples of 0.25).
- **Bugs the tests caught while building:** `hm_foreach` read `node->next` *after* the callback freed
  `node`. The teardown path does exactly that, so it was a use-after-free. Fixed by reading `next` first.
  And the first version of the "rehash is incremental" test only looked at a 4-bucket resize, which
  legitimately finishes in one step, so the test was wrong, not the table.

## Things to try next

- Persistence: an append-only file (log each write command, replay on start), then `BGSAVE`-style
  snapshots with `fork()`.
- Swap `poll()` for `epoll` (Linux) or `kqueue` (macOS). `poll()` rescans every descriptor on each call,
  which starts to matter around 10,000 connections.
- Hash tables and lists as value types (`HSET`, `LPUSH`/`BLPOP`). Blocking pops need a per-key queue of
  waiting clients plus the timer machinery that's already here.
- Seed the hash function per process (SipHash) so clients can't pick keys that all collide.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Based on the chapter list of "Build Your Own Redis with C/C++" by James Smith (https://build-your-own.org/redis/).
No code was copied from the book.
