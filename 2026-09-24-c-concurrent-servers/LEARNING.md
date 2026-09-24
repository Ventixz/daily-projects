# Programming Concurrent Servers (C)

**Source:** ["Concurrent Servers: Part 1 - Introduction"](https://eli.thegreenplace.net/2017/concurrent-servers-part-1-introduction/)
by Eli Bendersky, one of the C/C++ Network Programming entries in
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
(The tutorial's own site was unreachable while building this, so the toy
protocol below is my own design built for the same purpose the series
uses one for -- everything else, and the actual point of the exercise,
follows the source series: implement the same server three ways and
compare.)

Picked and built end-to-end in one sitting. No dependencies beyond the C
standard library, POSIX sockets/threads, and Linux's `epoll`/`timerfd`.

## What it is

The same tiny line-based TCP protocol, served three different ways, sharing
one implementation of the protocol itself:

- `src/common.c`/`common.h` -- `linebuf_t` (a growable buffer that turns a
  byte stream into complete lines, regardless of how the bytes were
  chunked) and `protocol_handle_line()` (parses one line: `ECHO`, `REV`,
  `UPPER`, `SLEEP <ms>`, `QUIT`). Unchanged across all three servers.
- `src/server_sequential.c` -- `accept()`, serve that one connection to
  completion, `accept()` again. ~25 lines.
- `src/server_threaded.c` -- identical accept loop, except each connection
  gets `pthread_create`'d off into its own detached thread.
- `src/server_epoll.c` -- one thread, one `epoll` instance, N connections.
  Nothing blocks: sockets are non-blocking, partial writes are buffered
  and finished on a later `EPOLLOUT`, and `SLEEP` is a `timerfd` registered
  with the same `epoll_wait()` instead of an actual `sleep()` call.

## Run it

```bash
cd 2026-09-24-c-concurrent-servers
make                    # builds ./sequential ./threaded ./epoll
make test-c             # unit tests for linebuf_t and the protocol parser
make test-py            # integration tests against all three live servers
make test               # both

./epoll 9090             # or ./sequential / ./threaded
# in another shell:
printf 'ECHO hello\nREV abcd\nUPPER shout\nQUIT\n' | nc 127.0.0.1 9090
```

## What it actually teaches

- **The concurrency model is the *only* difference between these three
  files.** `server_sequential.c` and `server_threaded.c` call the exact
  same `serve_blocking_connection()`; the diff between them is four lines
  (`pthread_create` + `pthread_detach` instead of a direct call). That
  alone is enough to turn "one slow client freezes the whole server" into
  "one slow client blocks only itself" -- `tests/test_concurrency.py`
  measures it directly: client B's `ECHO` gets answered in ~0.8s behind a
  sleeping client A on the sequential server, and in ~1ms on the threaded
  one, same protocol, same hardware, same two clients.

- **A blocking `accept()` loop doesn't just block *that connection* --
  it blocks *acceptance itself*.** The sequential server's slowness isn't
  "the request handler is slow," it's that `accept()` for client B is
  never even called until client A's connection object is fully done with
  (`SLEEP`, then `QUIT`). A second client sitting in the kernel's listen
  backlog is not "waiting," from the server's point of view it doesn't
  exist yet.

- **Non-blocking I/O means every blocking call needs a non-blocking
  replacement, not just `read`/`write`.** `SLEEP` looked like the easy
  command until the epoll server made "just call `usleep()`" into "freeze
  every connection this process owns for up to 10 seconds." The fix is
  `timerfd_create()` -- a timer that is *itself* a file descriptor `epoll`
  can multiplex alongside every socket, so "wait" becomes just another
  kind of "readable."

- **`send()` finishing is not guaranteed, and epoll makes you deal with
  it.** On a blocking socket, `send()` just doesn't return until everything
  is written (or the loop in `send_all()` retries). On a non-blocking one
  it can accept 0 bytes of a 4KB response if the kernel's send buffer is
  full, and there is no thread sitting around to retry it. `conn_send()` /
  `conn_try_flush()` in `server_epoll.c` buffer whatever didn't go out and
  ask `epoll` to notify on `EPOLLOUT`, which is the actual reason
  event-driven servers need a per-connection output buffer at all, not
  just a state machine for input.

- **Closed file descriptors get reused, and a design that assumes
  "this fd" means "this connection" will eventually answer the wrong
  client.** A `SLEEP 5000` that outlives its connection (client
  disconnects early) leaves a `timerfd` armed. By the time it fires, the
  integer that was that client's socket fd could easily have been handed
  by `accept()` to a completely unrelated, later connection. Each
  connection slot in `server_epoll.c` carries a generation counter that
  the pending timer captures and re-checks before sending anything --
  without it, this is a real, exploitable "wrong recipient" bug, not a
  hypothetical one. Verified under `valgrind --track-origins=yes` by
  abandoning a connection mid-`SLEEP` and immediately opening a new one
  (which the kernel promptly hands the same fd number): zero errors, and
  the stale timer is silently dropped instead of misdelivered.

## Known limitations (by design, to keep scope to ~2-4 hours)

- `epoll` mode is level-triggered with one `recv()` per readable event,
  not edge-triggered with a drain loop -- simpler and still correct, but
  not what you'd want at very high connection counts.
- Connection tables in `server_epoll.c` are flat arrays sized by
  `RLIMIT_NOFILE`, indexed directly by fd -- fine up to a few tens of
  thousands of fds, not a design for a fd space in the millions.
- No TLS, no HTTP -- this is deliberately the protocol-agnostic core the
  tutorial series is actually about, not a web server.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Credit: ["Concurrent Servers: Part 1 - Introduction"](https://eli.thegreenplace.net/2017/concurrent-servers-part-1-introduction/)
by Eli Bendersky.
