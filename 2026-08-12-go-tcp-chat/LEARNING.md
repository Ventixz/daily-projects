# Building a TCP Chat in Go (Go)

**Source:** ["Building a TCP Chat in Go"](https://www.youtube.com/watch?v=Sphme0BqJiY)
(video), from the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The video walks through a single-file broadcast server. I kept the brief --
a TCP server where any connected client's line reaches every other
client -- and designed against it fresh rather than following the video's
structure: a nickname handshake, `/nick`/`/list`/`/msg`/`/quit` commands,
and state logic that's fully unit-testable without opening a socket, none
of which the single-file version has room for.

## What it is

A multi-client TCP chat server. Connect with `nc localhost 6000` (or any
raw TCP client), pick a nickname, and anything you type gets broadcast to
everyone else connected, prefixed with your name.

- `internal/chat/hub.go` -- `Hub`, the only place client state lives:
  who's connected, under what nickname, who should receive a given line.
  `Join`, `Leave`, `Rename`, `Broadcast`, `Whisper`, `ListNicks`. No
  `net.Conn`, no `bufio`, nothing that requires a socket to exercise.
- `internal/chat/hub_test.go` -- 17 table-style tests plus one
  `-race`-driven concurrency test, calling `Hub` directly.
- `server.go` -- the only file that touches `net.Conn`: the accept loop,
  one goroutine per connection reading lines and dispatching them, one
  writer goroutine per connection draining that client's outbound channel.
- `server_test.go` -- 8 integration tests that dial real TCP connections
  against a server on an OS-assigned loopback port and assert on the
  bytes that come back.
- `main.go` -- `-addr` flag, then `select {}`.

## Run it

```bash
cd 2026-08-12-go-tcp-chat
go test ./... -race    # 25 tests, unit + integration, race detector clean
go run . -addr :6000    # then, in another terminal: nc localhost 6000
```

## What it actually teaches

- **A slow reader must not stall the whole room.** `Hub.deliverLocked`
  pushes onto a client's buffered channel with `select { case ch <- line:
  default: }` -- never a blocking send. `TestBroadcastDropsRatherThanBlocksWhenBufferIsFull`
  fills one client's buffer to capacity with nothing draining it, then
  broadcasts one more message and asserts the call returns (doesn't hang)
  and the buffer length is unchanged (the overflow message is dropped, not
  queued unbounded). Without the `default` case, one client that stops
  reading -- a dead connection the OS hasn't noticed yet, a slow terminal
  -- would freeze `Broadcast` for every other client, since `Hub`'s
  methods all hold the same mutex.
- **State mutation needs one lock, not one lock per map.** `Hub` has two
  maps that must stay in sync -- `clients` (the membership set) and
  `byNick` (the name index) -- guarded by a single `sync.Mutex`. Early on
  I had `Join` update `clients` and `byNick` as two separate critical
  sections; `TestConcurrentJoinBroadcastLeaveIsRaceFree` (50 goroutines
  joining, broadcasting, and leaving concurrently under `go test -race`)
  is what such a split would actually catch -- a goroutine could observe
  `clients` updated but `byNick` not yet, and a nickname collision check
  would race against it. One `Lock`/`defer Unlock` per exported method
  closes that window; the test exists specifically to make a regression
  back to finer-grained locking fail loudly instead of passing 999 times
  out of 1000.
- **The write side of a connection needs its own goroutine, or two
  broadcasts can interleave their bytes on the wire.** `net.Conn.Write` is
  not required to be atomic against concurrent callers, and `Broadcast`
  can be delivering to the same client from whichever connection called
  it. Each connection gets exactly one writer goroutine
  (`writeLoop` in `server.go`) that alone calls `conn.Write` for that
  client, fed by `client.Send`; every other goroutine that wants to talk
  to that client -- `Hub.deliverLocked`, or `dispatch`'s own command
  replies via `trySend` -- only ever posts to the channel, never touches
  the socket directly.
- **Shutdown order matters when a channel has multiple potential
  senders.** `client.Send` is closed by the connection's own goroutine in
  `handleConn`'s deferred cleanup, but only *after* `hub.Leave(client)`
  has returned. `Leave` removes the client from `Hub.clients` under the
  same mutex that `deliverLocked` reads before it ever sends -- so once
  `Leave` returns, no future `Broadcast`/`Whisper`/announcement can
  reach a channel that's about to be closed. Reordering those two lines
  (close first, `Leave` second) is a `send on closed channel` panic
  waiting for a concurrent broadcast to land in the gap; nothing in a
  single-connection manual test would surface it; it took a moment of
  reasoning through the two goroutines by hand.
- **A byte stream doesn't preserve write boundaries, and integration
  tests have to be written knowing that.** The server writes an
  unterminated `"Nick: "` prompt, then later a newline-terminated line
  from a completely separate goroutine (`writeLoop`, once `Join`
  succeeds). Both arrive on the same TCP stream; `server_test.go`'s
  `readLine()` helper reads to the next `\n` and strips a leading
  `"Nick: "` because that prompt and the first real line routinely
  arrive as one read from the client's side, even though the server
  issued them as two separate `Write` calls from two different
  goroutines.

## Deliberate scope cuts

- **No rooms/channels.** Everyone connected is in one broadcast group.
  Multiple rooms is a real feature (the video doesn't have it either) but
  it's an orthogonal piece of indexing on top of what's here, not a
  change to the concurrency shape this project is about.
- **No message history / late-join replay.** A client that connects mid-
  conversation sees nothing that happened before it joined, same as the
  video's version. Persisting and replaying history is a storage problem,
  not a networking one.
- **Line-oriented protocol, no framing beyond `\n`.** Fine for a
  `bufio.Scanner`-based text client; a binary protocol would need actual
  length-prefixed framing, which is a different lesson than the
  channel/goroutine one this project targets.

## What I'd add next

- **A `/who` reply that includes idle time or join time per user**, which
  would push `Client` to carry a timestamp and `ListNicks` to carry more
  than a name -- currently the only per-client data `Hub` exposes is the
  nickname string.
- **A configurable per-connection rate limit**, so `dispatch` can't be
  used to flood the broadcast channel of every other client; right now
  the only backpressure in the system is the *receive* side buffer
  (`sendBuffer`), not anything on the *send* side.
