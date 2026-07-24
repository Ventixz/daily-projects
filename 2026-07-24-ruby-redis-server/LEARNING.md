# Redis Server (Ruby)

**Source:** [Build your own Redis](https://rohitpaulk.com/articles/redis-1),
from [practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

The full series builds Redis up incrementally over five parts (PING, then RESP parsing, then
storage commands, then persistence, then replication). This build scopes to a standalone slice
of that: a real RESP-speaking TCP server with `PING`/`ECHO` and an in-memory `SET`/`GET`/`DEL`/
`EXISTS` store with key expiry, since that's where the protocol-design and concurrency lessons
live without needing an on-disk format.

## What it is

`lib/redis_server/resp.rb` encodes Ruby values into RESP (REdis Serialization Protocol) wire
bytes and parses client requests off a socket. `lib/redis_server/store.rb` is a thread-safe
in-memory key/value store with millisecond TTLs. `lib/redis_server/command_handler.rb` dispatches
a parsed command array to the store. `server.rb` ties them together with a `TCPServer` that hands
each connection its own `Thread`, all sharing one store. `test/server_test.rb` drives the whole
stack end-to-end over real `TCPSocket`s, the other test files cover each layer in isolation.

## Run it

```bash
cd 2026-07-24-ruby-redis-server
ruby server.rb                              # listens on 127.0.0.1:6379
PORT=7000 ruby server.rb                    # or pick a port
ruby -Ilib -Itest test/run_all.rb           # run the test suite (38 tests, no gems needed)
```

```
$ ruby server.rb &
$ redis-cli -p 6379 set foo bar
OK
$ redis-cli -p 6379 get foo
"bar"
$ redis-cli -p 6379 set temp v px 50
OK
$ sleep 0.1 && redis-cli -p 6379 get temp
(nil)
```

Any real Redis client works against it (`redis-cli`, language SDKs) since it speaks the actual
wire protocol - not just a toy request format built for this project.

## What it actually teaches

- **A wire protocol is a contract, not an implementation detail.** RESP's five reply types
  (simple string, error, integer, bulk string, array) are each length- or type-prefixed so a
  parser never has to guess where a value ends - `$6\r\nfoobar\r\n` says "6 bytes follow" instead
  of relying on a delimiter that could appear inside the data. Building `RESP.encode` and
  `RESP.parse_command` as the seam between "bytes on a socket" and "Ruby values the command
  handler cares about" is what let `redis-cli` talk to this server on the first try, with zero
  client-side code written for this project.
- **Lazy expiry avoids a whole class of scheduling problems.** The obvious design for TTLs is a
  background thread that sweeps expired keys on a timer. `Store#expire` does the opposite: it
  checks a key's deadline only when something asks for that specific key (`get`, `exists?`,
  `del`), and evicts it right then. No timer thread, no sweep interval to tune, no race between
  the sweeper and a concurrent write - the cost of checking one integer comparison on read is
  paid only by callers who actually touch that key.
- **One `Mutex` around the whole store is the right amount of locking here.** Every connection
  runs on its own `Thread`, so two clients can call `SET` at the same moment. Wrapping each
  `Store` method body in `@mutex.synchronize` serializes access to the shared `Hash`es without
  needing per-key locks or a lock-free structure - the store's methods are small and fast enough
  that lock contention was never the bottleneck worth solving for a learning project.
- **Parsing "just" a request line is still worth a real state machine.** `RESP.parse_command`
  has to handle two framings (a RESP array header, or a plain inline line for `nc`/telnet) and,
  within the array case, a fixed count of length-prefixed bulk strings. Writing it as a small set
  of single-purpose class methods (`parse_array`, `parse_bulk_string`, `parse_length`) that each
  raise `RESP::ProtocolError` on malformed input, rather than one method with nested rescues,
  made every parsing edge case (negative length, wrong element type, truncated stream) a one-line
  test instead of a hard-to-reproduce integration bug.
- **Errors are a value in the reply type, not an exception across the wire.** A malformed
  command (`SET` with one argument, an unknown verb) returns `RESP::Error.new("ERR ...")` from
  `CommandHandler#call` just like any other reply - it's encoded and sent back, and the
  connection stays open for the next command. Exceptions are reserved for things that actually
  break the connection (a protocol violation, a socket reset), which is why `serve` only rescues
  `RESP::ProtocolError` and `Errno::ECONNRESET`/`Errno::EPIPE`, not "anything that could go
  wrong."

## What I'd add next (stretch goals I skipped for scope)

- `INCR`/`DECR` and list commands (`LPUSH`/`RPUSH`/`LRANGE`), to see how the command-dispatch
  shape holds up once values aren't just strings.
- An `RDB`-style snapshot to disk on shutdown and reload on boot, which is where part 4 of the
  source series goes next - the store is already isolated behind a small interface, so swapping
  "just a Hash" for "a Hash backed by a snapshot file" shouldn't touch `CommandHandler` at all.
- A `MULTI`/`EXEC` transaction path, which would need to shift command execution from "handle
  immediately" to "queue per-connection, then replay atomically" - a genuinely different control
  flow from everything else here.
