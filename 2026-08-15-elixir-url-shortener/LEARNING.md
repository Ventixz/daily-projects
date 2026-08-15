# Building a Simple Chat App With Elixir and Phoenix (Elixir)

**Source:** ["Building a Simple Chat App With Elixir and Phoenix"](https://sheharyar.me/blog/simple-chat-phoenix-elixir/),
from the Elixir section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial wires the chat app up through a full Phoenix app --
channels, Endpoint, the whole asset pipeline. In keeping with every other
"build it yourself" project in this repo, I skipped the framework: no
Phoenix, no Cowboy, no Plug. What I kept from the brief is the two things
underneath it that Phoenix was actually hiding -- a raw socket protocol
layer, and OTP-native persistent shared state -- and built a different,
smaller thing with them: a URL shortener backed by Mnesia instead of a
chat room backed by channels. It's also a direct callback to this repo's
very first entry, `2026-07-15-go-url-shortener`: same problem, rebuilt on
completely different primitives (Mnesia tables and transactions instead
of whatever the Go version used for storage) to see what changes.

## What it is

A key-value HTTP microservice, same shape as `2026-08-13-rust-microservice`:
`POST /shorten` with a URL in the body creates a random 6-character code
and returns it; `GET /<code>` 302-redirects to the stored target and bumps
a click counter; `GET /<code>/stats` reports that counter.

- `lib/url_shortener/store.ex` -- all persistence, via two Mnesia tables:
  `:urls` (`code -> target`) and `:url_hits` (`code -> click count`).
  Nothing above this module knows Mnesia exists.
- `lib/url_shortener/http.ex` -- `parse_request/1` (buffer of bytes ->
  `%Request{}`, socket-free), `build_response/3` (status/headers/body ->
  bytes), and `route/1` (`%Request{}` -> `{status, headers, body}`,
  also socket-free -- calls `Store` but never touches a socket).
- `lib/url_shortener/server.ex` -- the only module that touches
  `:gen_tcp`: accepts connections and drives the read/parse/route/write
  cycle for each one, one Task per connection, supervised.
- `test/` -- 27 ExUnit tests: pure parsing/response-building tests, a
  `route/1` suite against hand-built `%Request{}` values, a concurrency
  test against `Store` directly, and an integration suite that opens real
  TCP sockets against the actually-running application.

## Run it

```bash
cd 2026-08-15-elixir-url-shortener
mix test                                             # 27 tests
mix run --no-halt                                    # listens on :4000
curl -i -X POST -d 'https://example.com' http://127.0.0.1:4000/shorten
curl -i http://127.0.0.1:4000/<code>                 # 302 to the target
curl -i http://127.0.0.1:4000/<code>/stats           # click count
```

## What it actually teaches

- **Declaring an OTP dependency changes *when* it starts, and that
  timing mattered here.** `mix.exs` lists `:mnesia` under
  `extra_applications` so `mix xref` doesn't flag every `:mnesia.*` call
  in `store.ex` as reaching into an undeclared application. That
  declaration means OTP starts Mnesia -- fresh, schema-less, RAM-only for
  this node -- *before* `UrlShortener.Application.start/2` ever runs. My
  first version of `Store.init_schema/0` didn't know that and went
  straight to `:mnesia.create_schema([node()])` followed by
  `create_table(..., disc_copies: [node()])`. It failed on every run with
  `{:aborted, {:bad_type, :urls, :disc_copies, :nonode@nohost}}`, because
  `create_schema/1` against an already-running Mnesia is a no-op (it just
  reports the schema exists) -- the RAM-only schema OTP had already
  brought up was the one still in effect, and no amount of retrying
  `create_table` changes a schema that's already there. The fix is one
  line, `:mnesia.stop()`, at the top of `init_schema/0` (`store.ex`):
  stop the auto-started instance, write the on-disk schema while nothing
  is running, then start Mnesia back up against that schema. Declaring
  the dependency and controlling its startup order both turned out to
  matter, and satisfying the first one is what broke the second.
- **A table's shape decides which Mnesia ops it can use, so the shape is
  a real design decision, not just a struct definition.**
  `:mnesia.dirty_update_counter/3` -- an atomic, lock-free increment --
  only works on records shaped `{Table, Key, Integer}`. I originally
  planned one `:urls` table holding `{code, target, hits}`, which is
  arity-4 with `target` sitting between the key and the counter; that
  shape can't use `dirty_update_counter` at all. Splitting into `:urls`
  (`{code, target}`) and a separate `:url_hits` (`{code, hits}`) -- the
  latter now the exact shape the atomic op requires -- is what makes
  `Store.resolve/1` able to use it. The identity mapping and the
  analytics counter didn't need to live in the same record, and keeping
  them apart is what unlocked the cheaper, lock-free path for the
  counter.
- **"Dirty" Mnesia ops aren't dirty by degree -- an atomic dirty op and a
  pair of racing dirty ops are different things wearing the same
  adjective.** `Store.resolve/1` looks a code up with `dirty_read/1` (no
  lock, and none needed -- a microseconds-stale existence check is
  harmless) but bumps the click counter with `dirty_update_counter/3`
  rather than a `dirty_read` + `dirty_write` pair, because the latter is
  two independent operations with nothing enforcing atomicity between
  them: two processes can both read `n` before either writes `n + 1`, and
  one increment vanishes. I built the read+write version first and ran
  `test/url_shortener/store_test.exs`'s
  `100 concurrent resolves against the same code all count` against it --
  100 processes hitting one code's counter concurrently landed at 84, not
  100. Swapping in `dirty_update_counter/3` (still no transaction, still
  "dirty") made every run land on exactly 100, because Mnesia implements
  it as one atomic operation on the table rather than two ops with a race
  window between them.
- **A write-locking read is what actually prevents a check-then-act
  race; a transaction alone doesn't.** `Store.create_short/1` picks a
  random 6-character code and, inside `:mnesia.transaction/1`, checks
  whether it's taken before writing it. That check uses `:mnesia.wread/1`
  (a *write-locking* read), not the plain `read/1` a first pass at this
  would reach for. A plain read only takes a read lock, so two
  transactions can both read "code is free," both proceed to write, and
  the second write silently overwrites the first caller's mapping with
  its own target -- a genuine lost-update bug even though both writes
  happen inside otherwise-correct transactions. `wread` takes the write
  lock at the point of the read, so the second transaction blocks until
  the first commits, then sees the row as taken and retries with a fresh
  code (`store::generate_unique_code`'s recursion) instead of racing past
  it. `store_test.exs`'s
  `each create_short call gets a distinct code` fires 50 of these and
  asserts on `Enum.uniq/1`.
- **A buffer has to be re-parsed from scratch, not read incrementally,
  when the transport doesn't respect message boundaries.**
  `:gen_tcp.recv/3` in passive mode hands back whatever bytes are
  currently available, not "one HTTP request" -- the same lesson
  `2026-08-13-rust-microservice`'s `BufReader` write-up already pinned
  for Rust, showing up again here for a different reason. `Http.parse_request/1`
  (`http.ex`) takes the *whole* accumulated buffer every time, re-scans
  for `\r\n\r\n`, and re-checks the trailing byte count against
  `Content-Length` before declaring the body complete; `Server.read_request/2`
  just keeps calling it with a growing buffer until it stops returning
  `:incomplete`. `integration_test.exs`'s
  `a request that arrives byte-by-byte still parses correctly` sends a
  real POST one byte per `:gen_tcp.send/2` call to prove this holds even
  in the worst case, not just the two-or-three-chunks case unit tests
  tend to cover.
- **`Content-Length` recomputed at write time, now in a third language.**
  `Http.build_response/3` drops any caller-supplied `Content-Length` and
  always writes `byte_size(body)`, the identical rule
  `2026-08-13-rust-microservice/src/http.rs`'s `Response::write` and
  `2026-08-09-java-http-server`'s `HttpResponse.write()` both enforce.
  `http_test.exs`'s
  `recomputes Content-Length from the actual body regardless of what's
  passed in` sets a wrong length via the headers map first, same as the
  Rust project's equivalent test, to prove the write path overrides
  rather than merely defaults.

## Deliberate scope cuts

- **No keep-alive.** Every connection is one request, one response, then
  `:gen_tcp.close/1`. Same cut `2026-08-13-rust-microservice` made, same
  reason: this project's lesson is Mnesia and buffered parsing, not the
  connection-lifecycle state machine keep-alive needs.
- **No supervision restart strategy beyond `:one_for_one` with the
  default intensity.** Each connection running under
  `UrlShortener.TaskSupervisor` means one bad request can't take down the
  acceptor loop, but there's no circuit breaker if a whole class of
  requests starts crashing repeatedly -- OTP's default max-restarts
  ceiling would eventually give up and crash the application, which is
  probably the right behavior for a learning project but not for
  anything actually deployed.
- **Codes are short and collision-checked at write time, not
  collision-free by construction.** Six characters from a 62-symbol
  alphabet is ~5.6e10 possible codes with no reservation scheme; fine at
  the scale a learning project runs at, wrong for a shortener that
  expects to mint billions of codes, where the retry-on-collision loop in
  `generate_unique_code/2` would start mattering for latency.

## What I'd add next

- **Node-to-node Mnesia replication.** The whole point of Mnesia over an
  ETS table is that it's built to run distributed across BEAM nodes with
  automatic replication; this project only ever runs `disc_copies` on a
  single `nonode@nohost`, so none of that is actually exercised here.
- **A `DELETE /<code>` HTTP route.** `Store.delete/1` exists and is
  tested directly, but nothing in `Http.route/1` calls it -- there's no
  way to remove a mapping over the wire right now.
- **Rate limiting on `POST /shorten`,** since right now nothing stops one
  client from exhausting a meaningful slice of the code space by looping
  the endpoint.
