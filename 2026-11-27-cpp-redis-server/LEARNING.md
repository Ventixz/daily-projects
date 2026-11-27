# Learning: Build Your Own Redis with C/C++

**Source:** *Build Your Own Redis with C/C++* by James Smith
(<https://build-your-own.org/redis/>), listed in the C/C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
build-your-own.org could not be reached from the build environment. So this project follows the
book's **plan**: a poll() event loop, a length-prefixed binary protocol, a hashtable that resizes
gradually, a sorted set built on an AVL tree, idle timers, TTLs kept in a heap, and a thread pool.
It does **not** follow the book's text or code. Everything here was written from scratch in
C++17 using only the standard library and POSIX.

> Written by the daily-project routine (Claude), not by the repo owner.

## What it is

```
$ make && ./build/server &          # listening on 127.0.0.1:1234
$ ./build/client set greeting hello -- get greeting -- zadd scores 1.5 alice \
                 -- zadd scores 2 bob -- zquery scores 0 "" 0 10 -- zadd greeting 1 x
(nil)
(str) hello
(int) 1
(int) 1
(arr) len=4
(str) alice
(dbl) 1.5
(str) bob
(dbl) 2
(arr) end
(err) 3 expect zset
```

`--` separates commands. The client writes all of them at once (pipelining) and then reads the
replies in order.

| Command | Reply |
|---|---|
| `ping [msg]` | `PONG` or `msg` |
| `get key` / `set key value` / `del key` | value or nil / nil / 1 or 0 |
| `keys` / `dbsize` | every live key / how many there are |
| `pexpire key ms` | 1 if the key exists. A negative `ms` removes the TTL |
| `pttl key` | ms left, `-1` if there is no TTL, `-2` if there is no such key |
| `zadd key score name` | 1 if added, 0 if the score was updated |
| `zrem key name` / `zscore key name` / `zrank key name` | 1/0 / score or nil / 0-based rank or nil |
| `zquery key score name offset limit` | up to `limit` (name, score) pairs, starting `offset` places after the first member ≥ (score, name) |

## How it is built

```
src/
  protocol.*     framing + tagged values (nil/err/str/int/dbl/arr), both directions
  hashtable.*    intrusive chained hashtable with progressive rehashing
  avl.*          intrusive AVL tree that also stores subtree sizes (for offset and rank)
  zset.*         sorted set = hashtable (by name) + AVL tree (by score, name), sharing nodes
  heap.*         min-heap of deadlines where each item knows its own index
  list.h         intrusive doubly linked list (idle connections, oldest first)
  thread_pool.*  workers that free huge sorted sets off the main thread
  store.*        keyspace + commands; the current time is passed in as a parameter
  server.cpp     the poll() event loop
  client.cpp     CLI client
```

### 1. The event loop (`server.cpp`)

The server has one thread and one loop. Each pass does this:

1. Build the `pollfd` list. The listening socket always asks for `POLLIN`. A connection asks
   for `POLLIN` *or* `POLLOUT`, depending on whether it is reading requests or still sending
   replies.
2. Work out the poll timeout from the **nearest timer**: the oldest idle connection or the
   earliest key deadline. With nothing scheduled, the timeout is -1 and the loop sleeps until
   there is I/O.
3. Accept new clients until `EAGAIN`. For each ready socket, read once, run **every complete
   request** in the buffer, and try to write the replies right away.
4. Close connections that have been idle too long, then expire due keys.

Every socket is non-blocking, so a slow client can never stall the loop. Two details matter:

- **Read, then stop reading.** Once there are replies waiting to go out, the connection switches
  to write mode and stops reading. A client that pipelines faster than it reads gets slowed down
  by TCP itself, instead of making the server buffer without limit.
- **A broken frame closes the connection.** After a malformed length prefix there is no way to
  find where the next message starts, so the connection is closed instead of guessed at.

### 2. The protocol (`protocol.*`)

```
request : u32 len | u32 nstr | (u32 len | bytes) * nstr
response: u32 len | tag u8 | payload       (ARR nests: u32 n | value * n)
```

Both directions are length-prefixed. The parser can therefore always say "incomplete" or "ok"
before it parses anything, and it treats a byte-by-byte trickle like any other input. The
parser is strict: a frame over 32 MiB, a string longer than its frame, bytes left over after
the last string, or more than 200k arguments all count as errors. The response decoder is just
as strict. An array that claims a million elements inside a 5-byte body is rejected before
anything is allocated. A reply that would grow past the size limit is swapped for
`ERR_TOO_BIG`. To make that possible, the length prefix is reserved first and filled in once
the reply is written.

### 3. Progressive rehashing (`hashtable.*`)

A normal hashtable doubles its array and moves every key in one step, which is an O(n) pause.
On a server with a million keys, every client waits through that pause. Here, a resize only
*allocates* the bigger table. After that, every lookup, insert or delete first moves
`k_rehashing_work = 128` nodes from the old table to the new one. A lookup checks both tables
while the move is in progress. The test `hashtable_rehash_is_incremental` checks this directly:
one lookup in the middle of a rehash moves exactly 128 nodes, and every key can still be found
during the move.

The table is **intrusive**. It never allocates nodes. Callers inherit from `HNode`, and the
table links those nodes together. `ZNode : AVLNode, HNode` lives in two structures at once with
no extra allocation, and `static_cast` gets back from either base to the node, with no
`offsetof` tricks.

### 4. Sorted set = hashtable + AVL tree (`zset.*`, `avl.*`)

- The **hashtable** keyed by name answers `zscore`, `zrem` and "is this member already here?"
  in O(1).
- The **AVL tree**, ordered by `(score, name)`, keeps members sorted for range queries. Each
  node also stores its subtree size (`cnt`). With that, "the node *k* places after this one"
  (`avl_offset`) and "this node's rank" (`avl_rank`) are O(log n) instead of O(k). That is how
  `zquery … offset limit` and `zrank` are cheap even deep into the set.
- Changing a score means removing the node from the tree and inserting it again. The hashtable
  entry does not move.
- Deleting a node with two children puts its in-order successor in its place. The successor is
  removed first, and only *then* are the deleted node's links copied, because rebalancing may
  have rotated the node that is going away.

### 5. Timers (`list.h`, `heap.*`)

- **Idle connections.** Every connection has the same timeout, so connections sorted by "last
  active" are also sorted by deadline. That order is just a linked list: on each activity the
  connection moves to the tail, and the head is always the next to expire. All operations are
  O(1).
- **Key TTLs.** Deadlines differ per key, so they go in a min-heap. Each heap item holds a
  pointer to the key's `heap_idx`, and the heap updates it every time the item moves. A key can
  then change or remove its own TTL in O(log n) without searching for it.
- **Two kinds of expiry.** The timer deletes due keys even when no client is connected (tested
  with no traffic at all). It removes at most 2000 keys per loop pass, so a burst of identical
  deadlines cannot freeze the server. `lookup()` also checks the deadline, so a key that is due
  but not yet collected is never returned. Time is a parameter of `Store::execute`, which lets
  the unit tests move the clock forward instead of sleeping.

### 6. Freeing big values on a worker thread (`thread_pool.*`)

`DEL` on a sorted set with a million members would spend a long time in `delete` on the loop
thread. Above 1000 members, the entry is first unlinked from the keyspace and the TTL heap on
the main thread, then handed to a worker to free. No other thread can reach it by then, so the
freeing needs no locks.

## Running it

Linux or macOS with a C++17 compiler (on Windows, use WSL: the server uses POSIX sockets and
`poll`).

```
make                 # build/server, build/client
make test            # unit + integration tests, built with ASan + UBSan
./build/server --port 1234 --idle-ms 5000
./build/client --port 1234 ping
```

## Tests (31, all under AddressSanitizer + UndefinedBehaviorSanitizer)

**Unit (21)** — `test/unit_tests.cpp`, no network:
- hashtable: 100k insert/lookup/delete, and rehashing that moves exactly 128 nodes per
  operation, with every key findable mid-rehash
- AVL: 4000 random insert/delete operations checked against `std::multiset`, plus heights,
  counts, parent links and the balance rule after each batch; 1023 sequential inserts stay
  within height 11; `avl_offset`/`avl_rank` checked exhaustively for every tree size 1–59
- zset: (score, name) ordering, score updates, seekge, ranks, 5000 random operations against a
  `std::map`, binary-safe names
- heap: 20k random upsert/delete operations, then the heap property and every back-reference
  are checked
- protocol: request round-trips; every strict prefix is "incomplete"; five kinds of malformed
  frames; tagged responses; an array that lies about its length; an oversized reply turning
  into `ERR_TOO_BIG`
- commands: strings, errors, case-insensitive names, `keys`, TTLs with an injected clock (lazy
  expiry exactly at the deadline, a negative TTL removing it, `set` clearing it), the
  2000-key-per-pass expiry cap, zset commands including negative offsets, and large sets freed
  on a worker (ASan would report a leak if the worker didn't free them)

**Integration (10)** — `test/integration_tests.cpp` starts the real server on a random port:
basic commands; 2000 pipelined requests answered in order; a request sent one byte at a time; a
20 MiB value both ways; an oversized frame and a malformed frame each closing only that
connection; an idle client closed while a busy one stays up; TTL keys removed with no clients
connected; 64 clients with requests in flight at once; a client that disconnects with 40 MiB of
replies queued. Every test also checks that SIGTERM gives a clean exit with status 0, which
under ASan also means no leaks.

## Things worth noticing

- **Parsing and execution are separate from I/O.** `parse_request` works on bytes and
  `Store::execute` works on arguments. Neither knows about sockets, which is why most of the
  tests need no network.
- **Pipelining comes for free with framing.** Since every message carries its length, the
  server just loops "parse one, run it" over its buffer. The client sends 2000 requests in one
  `write`, and nothing special is needed on either side.
- **Every limit is enforced at the edge.** Message size, argument count, response size, nesting
  depth and expiry work per pass are all bounded, so no single client or burst of deadlines can
  make the server allocate without limit or stall.

## Stretch ideas

- Replace `poll` with `epoll`. Rebuilding the `pollfd` array every pass is O(connections).
- Add a cap on output buffer size per client, the way Redis drops clients whose replies pile
  up.
- Add persistence: an append-only file replayed at startup.
