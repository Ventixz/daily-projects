# Learning: Build Your Own Redis with C/C++ (an in-memory key-value server)

**Source:** "Build Your Own Redis with C/C++" by James Smith
(<https://build-your-own.org/redis/>). It is listed in the C/C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
build-your-own.org could not be reached from the build environment, so this project follows the
book's **outline**: a length-prefixed binary protocol, a `poll()` event loop, a hash table that
resizes progressively, sorted sets built on an AVL tree, idle and TTL timers, and a thread pool. It
does **not** follow the book's text or code. Everything is written from scratch in C++17 with only
the standard library and POSIX sockets.

> Written by the daily-project routine (Claude), not by the repo owner.

## What it is

```
$ build/server --port 6380
listening on 127.0.0.1:6380

$ client set greeting hello            $ client zadd board 3120 ada
(nil)                                  (int) 1
$ client get greeting                  $ client zadd board 2890 grace
(str) hello                            (int) 1
$ client pexpire greeting 60000        $ client zadd board 3305 linus
(int) 1                                (int) 1
$ client pttl greeting                 $ client zrank board ada
(int) 59998                            (int) 1
$ client get board                     $ client zquery board 3000 '' 0 10
(err) 3 expect string type             (arr) len=4
                                       (str) ada
                                       (dbl) 3120
                                       (str) linus
                                       (dbl) 3305
                                       (arr) end
```

(That output is from a real run; the two columns were one session.)

- **Server** (`server.cpp`): one thread, non-blocking sockets, `poll()`. It handles pipelined
  requests, applies backpressure, closes idle connections and stops cleanly on SIGINT/SIGTERM.
- **Commands** (`store.cpp`): `ping get set del keys dbsize pexpire persist pttl` for strings, and
  `zadd zrem zscore zrank zcard zquery` for sorted sets. Command names are case-insensitive.
- **Data structures**, all intrusive and all hand-written:
  - `hashtable.cpp`: a chained hash map that resizes a little at a time
  - `avl.cpp`: an AVL tree that stores subtree sizes
  - `heap.cpp`: a min-heap with back-references
  - `dlist.h`: a circular doubly linked list
- **Client** (`client.cpp`): runs one command from argv, or one command per line from stdin.
  Replies are printed in the format shown above.

## Run it

You need g++ 11 or newer (or clang 14+; `std::to_chars` for doubles) and `make` on Linux or macOS.

```
make                 # build/server and build/client
make test            # 31 unit tests + 13 end-to-end tests against the real binary, ~2 s
make asan            # the same tests under AddressSanitizer + UndefinedBehaviorSanitizer
make tsan            # the same tests under ThreadSanitizer
build/server --port 6380 --idle-timeout-ms 5000 --threads 4
build/client --port 6380 set name "Ada Lovelace"
```

**Windows 11:** the server uses POSIX sockets and `poll()`, so build it inside WSL
(`wsl --install -d Ubuntu`, then `sudo apt install g++ make` and the commands above). A native
port would need Winsock and `WSAPoll`. I did not do that or test it.

## The protocol

```
request:   u32 len | u32 nstr | nstr × (u32 len | bytes)
response:  u32 len | value
value:     NIL | ERR code msg | STR bytes | INT i64 | DBL f64 | ARR n values   (1-byte tag first)
```

All integers are little-endian, and each message is limited to 32 MiB. Arguments are byte strings
with a length prefix, so keys and values can contain spaces, newlines or NUL bytes. Replies are
self-describing, so the client decodes them without knowing which command it sent.

## Tests (44 tests, about 2 s; also clean under ASan/UBSan and TSan)

- **Hash map (4):** FNV-1a test vectors. A resize is checked *while it is in progress*: both tables
  hold keys, every key can still be found, and iteration still sees all of them. Lookups alone
  finish the resize. 200,000 random operations are checked one by one against
  `std::unordered_set`.
- **AVL (3):** 1,000 sorted inserts stay at height ≤ 15. 50 rounds of random inserts and deletes
  (with duplicates) check heights, balance, subtree sizes, parent pointers and order after every
  operation. `avl_offset` is checked from each of 300 nodes to every other node and to both ends.
  `avl_rank` is checked for every node.
- **Sorted set (4):** insert, update and remove. Ordering by (score, name). `seek_ge` and offsets.
  20,000 random operations are checked against a `std::set<pair<double,string>>`, including ranks.
- **Heap (2):** after random set/update/remove operations, every back-reference equals its index,
  and removing items from the top gives them in sorted order.
- **Protocol (5):** the exact bytes of a request. Round trips with empty, binary and 70 KB
  arguments. Every truncation point of a request and of a reply is rejected, along with trailing
  bytes, too many arguments, a length larger than the buffer, an "array of 4 billion" sent in 5
  bytes, and nesting deeper than 64.
- **Store (10)**, using a fake clock, so TTL tests need no sleeps:
  - A key is still readable 1 ms before it expires and gone exactly at the deadline, even if the
    timer has not run yet.
  - Active expiry stops after its per-call work limit.
  - SET clears a TTL. PERSIST works. A PEXPIRE of 0 or less deletes the key. A TTL near INT64_MAX
    is clamped instead of overflowing.
  - Wrong types, wrong argument counts and bad numbers (`nan`, `" 10"`, `10x`, out of range) give
    errors.
  - Paging through 95 members with `zquery` sees each member exactly once.
  - A large set gets freed on the thread pool.
- **Thread pool (3):** all queued jobs run before destruction, jobs run on a worker thread, and
  with 0 threads jobs run inline.
- **End to end (13)**, against the real `build/server` started on port 0:
  - 1,000 pipelined requests sent in one write get their replies in order.
  - A request sent one byte at a time works.
  - A 20 MiB value round-trips.
  - An oversized length prefix closes only that connection.
  - A malformed body closes the connection.
  - A `keys` reply over 32 MiB becomes `ERR_TOO_BIG`, and the connection still works afterwards.
  - An idle connection is closed after about 300 ms while a busy one stays open.
  - A TTL expires in the background (checked with `dbsize`, which does not trigger lazy expiry).
  - Deleting a sorted set with 200,000 members does not delay the next `ping`.
  - 64 clients take turns.
  - The `client` binary works in both argv mode and stdin mode.
  - SIGTERM gives exit status 0. Under ASan this also means nothing leaked at shutdown.

I checked that the tests can fail by breaking the code on purpose, one change at a time:

| Bug introduced | Result |
|---|---|
| AVL `cnt` ignores the right subtree | 3 AVL tests fail |
| Rehashing loses nodes | the unit tests crash, and 3 end-to-end tests fail |
| `heap_up` does not update back-references | 2 heap tests fail |
| Only the first pipelined request is handled | 2 end-to-end tests fail |
| No lazy expiry on lookup | the fake-clock TTL test fails |

## Layout

```
src/
  protocol.h/.cpp     framing, request parsing, TLV reply encode/decode, redis-cli formatting
  hashtable.h/.cpp    intrusive chained hash map, progressive rehashing, FNV-1a
  avl.h/.cpp          intrusive AVL tree with subtree counts: fix, delete, offset, rank
  zset.h/.cpp         sorted set = hash map (by name) + AVL tree (by score, name)
  heap.h/.cpp         min-heap of expiry times with back-references
  dlist.h             intrusive doubly linked list (idle-connection order)
  thread_pool.h/.cpp  mutex + condition variable worker pool
  store.h/.cpp        keyspace and all commands; no sockets, injectable clock
  server.cpp          poll() event loop, connections, timers, signals
  client.cpp          CLI client
tests/
  testing.h, test_main.cpp      a ~90-line test framework
  test_*.cpp                    one file per module; test_server.cpp spawns the real binary
Makefile
```

## What this project teaches

1. **TCP is a byte stream, not a message stream.** A single `read()` can return half a request,
   or three and a half. The server appends whatever arrives to a buffer. It then handles every
   *complete* length-prefixed message in it, and leaves the rest for the next read. Two tests check
   this from both directions: one byte per write, and 1,000 requests in one write. A length prefix
   above the limit can't be skipped safely (you can't find where the next message starts), so the
   only safe response is to close the connection.
2. **The event loop is a state machine for each connection.** Each `Conn` has `want_read`,
   `want_write` and `want_close`, and `poll()` is asked only about what each connection wants.
   Once replies are queued, the connection stops reading until they are sent. That is the
   **backpressure**: a client that pipelines without reading its replies stops being served
   instead of making the server's buffers grow forever. After processing, the server also tries to
   write straight away instead of waiting for the next `poll()`, because the socket is almost
   always writable. That saves a full loop iteration on every request.
3. **Avoid paying for a resize all at once.** A normal hash table rehashes all n keys when it
   grows. With millions of keys, that one request stalls every client. Here the old table is kept,
   and each later operation moves up to 128 nodes. Lookups check both tables until the move is
   done. The work is the same in total, but it is spread over many requests, so latency stays flat.
4. **Intrusive data structures in C++ are just inheritance.** The book uses `container_of` and
   `offsetof` to get from an embedded node back to its owner. In C++, `struct ZNode : AVLNode, HNode`
   lets `static_cast` do the same pointer adjustment, with type checking and without the
   `offsetof`-on-non-standard-layout warning. One allocation is in two indexes at once (by name and
   by score), and freeing it means unlinking it from both.
5. **Augmenting a balanced tree with subtree sizes gives you rank queries for free.** If each AVL
   node stores how many nodes are under it, then "what rank is this member" and "go 50 places
   forward" both take O(log n). This is what makes `zquery … offset limit` paging cheap. The cost is
   one more field to update in every rotation, and the tests show that getting it wrong breaks
   offsets and ranks without breaking ordering.
6. **Timers decide how long `poll()` sleeps.** There are two kinds of timer:
   - Idle connections all have the same timeout, so a linked list ordered by last activity is
     enough. Touching a connection moves it to the back, and the front is always the next to
     expire.
   - TTLs are arbitrary, so they need a min-heap. Each heap item points at its key, and each key
     stores its heap index, so changing or removing a TTL is O(log n).

   `poll()`'s timeout is the nearest deadline from either one. Expiry also happens on lookup, so a
   key is never returned after its deadline even when the timer is behind. The timer deletes at
   most 2,000 keys per loop iteration so that a mass expiry can't stall the loop.
7. **Freeing memory can be slow.** Freeing a sorted set with millions of members means visiting
   millions of nodes. Sets with more than 1,000 members are handed to a worker thread to free. The
   event loop stays single-threaded, and the worker touches only memory nothing else can reach
   any more, so there is nothing to lock beyond the job queue. TSan confirms this.

## Where it differs from the book

- **C++ idioms instead of C-in-C++.** I used inheritance in place of `container_of`, plus
  `std::string`, `std::unique_ptr` and `std::thread`. The data-structure algorithms (rehash
  migration, AVL fix and delete, offset walk, heap sifts) follow the same ideas as the book.
- **Closer to Redis in a few places:** `SET` replaces a value of any type and clears its TTL. A
  `PEXPIRE` of 0 or less deletes the key. Emptying a sorted set deletes the key. `KEYS` skips keys
  that have expired but have not been collected yet. I also added `PERSIST`, `ZRANK`, `ZCARD`,
  `DBSIZE` and `PING`.
- **The keyspace is separated from the network.** `Store` takes the time as a parameter, so TTL
  behaviour is tested exactly with a fake clock.
- **Not compatible with Redis.** The wire format is the book's binary protocol, not RESP, so
  `redis-cli` and Redis client libraries will not work with this server.

## Ideas for next steps

- Speak RESP so `redis-cli` and `redis-benchmark` work, then compare throughput with real Redis.
- Use `epoll` (Linux) or `kqueue` (macOS) instead of `poll()`, which rebuilds and rescans the
  whole fd list on every iteration.
- Persistence: an append-only log of write commands, replayed on startup.
- `ZRANGEBYSCORE` / `ZREMRANGEBYRANK` on top of `seek_ge` and `avl_offset`.

## License

Licensed under the MIT License; see the LICENSE file at the repository root. The structure follows
"Build Your Own Redis with C/C++" by James Smith (<https://build-your-own.org/redis/>); no text or
code from it is copied here.
