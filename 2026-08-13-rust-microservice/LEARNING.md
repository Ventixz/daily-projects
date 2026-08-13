# Write a Microservice in Rust (Rust)

**Source:** ["Write a Microservice in Rust"](http://www.goldsborough.me/rust/web/tutorial/2018/01/20/17-01-11-writing_a_microservice_in_rust/),
from the Rust section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial's brief is a small HTTP service in front of a shared,
concurrently-accessed collection, built with the `hyper` crate. I kept the
brief -- HTTP in front of shared mutable state, hit from multiple clients at
once -- but built it on `std::net` only, no `hyper`, no `tokio`, no crates
at all, for the same reason this repo's other "build it yourself" projects
skip frameworks: the interesting bugs live in the state-sharing and
protocol-parsing code, and a framework's job is to hide exactly that code
from you.

## What it is

A key-value HTTP microservice: `POST /items` creates a record and hands
back its id, `GET/PUT/DELETE /items/{id}` operate on one record, `GET
/items` lists everything. Every value is an opaque byte string -- the store
never looks inside it.

- `src/store.rs` -- `Store`, the only place state lives: an
  `RwLock<HashMap<u64, Vec<u8>>>` plus an `AtomicU64` id counter.
  `create`/`get`/`put`/`delete`/`list`, no `TcpStream` anywhere in the
  file, fully testable by calling it directly.
- `src/http.rs` -- `Request::parse` (request line, headers,
  `Content-Length`-bounded body) and `Response::write`, both working
  against `impl BufRead` / `impl Write` rather than a live socket.
- `src/server.rs` -- `route()` maps method+path to a `Store` call and is
  itself socket-free (unit tested with hand-built `Request` values); the
  connection-handling code around it is the only part that touches
  `TcpListener`/`TcpStream`.
- `tests/integration.rs` -- 5 tests that spawn a real server on an
  OS-assigned port and drive it over actual `TcpStream` connections,
  including one that fires 32 concurrent `POST`s down 32 separate sockets.
- 26 tests total (21 unit, 5 integration), `cargo clippy --all-targets`
  clean.

## Run it

```bash
cd 2026-08-13-rust-microservice
cargo test                                  # 26 tests, no external deps
cargo run --release -- 127.0.0.1:7878        # then, in another terminal:
curl -i -X POST -d 'hello world' http://127.0.0.1:7878/items
curl -i http://127.0.0.1:7878/items
curl -i http://127.0.0.1:7878/items/1
curl -i -X PUT -d 'updated' http://127.0.0.1:7878/items/1
curl -i -X DELETE http://127.0.0.1:7878/items/1
```

## What it actually teaches

- **Two id-issuing paths sharing one namespace need to agree, or they'll
  eventually collide.** `POST` allocates ids from `next_id.fetch_add(1,
  ..)`; `PUT /items/{id}` lets the *client* pick the id, for upsert
  semantics. My first version of `Store::put` just inserted at the given
  id and left `next_id` alone -- which meant a client that PUT
  `/items/500` today could get silently overwritten by whatever `POST`
  eventually counts up to 500. The fix is one line,
  `next_id.fetch_max(id + 1, Ordering::Relaxed)`, in `Store::put`
  (`store.rs`), which folds every client-supplied id into the same
  counter POST draws from. `store::tests::a_put_id_reserves_the_id_space_against_future_creates`
  and the integration-level `put_reserves_its_id_against_future_posts`
  both pin this from two different altitudes -- I wrote the first version
  without the `fetch_max` line, watched the unit test fail with a
  collision, and only then understood why the two id sources needed to be
  reconciled at all.
- **`fetch_add` is safe under concurrency for a reason worth stating
  precisely: allocating the id and reserving it are the same atomic
  step.** It's tempting to write `let id = self.next_id.load(..); self.next_id.store(id + 1, ..);`
  as two statements, which reads fine until two threads interleave
  between the load and the store and walk away with the same id. `Store::create`
  never has that window because `fetch_add` *is* the read-and-increment,
  indivisibly. `store::tests::concurrent_creates_never_hand_out_the_same_id`
  spawns 200 threads calling `create` at once and asserts on a `HashSet`
  of the results; the integration suite's
  `concurrent_posts_never_collide_on_an_id` repeats the same assertion one
  layer up, through 32 real TCP connections instead of 32 threads calling
  `Store` directly, so a bug introduced anywhere between the socket and
  the atomic counter would still be caught.
- **A `RwLock` poisons itself for every future caller, not just the
  thread that panicked.** If a thread panics while holding a write guard,
  every subsequent `.read()`/`.write()` on that same lock returns `Err`
  forever -- one bad request handler would take the entire store down for
  every other client, not just the one that hit the bug. `Store::read`
  and `Store::write` (`store.rs`) both recover with
  `.unwrap_or_else(|poisoned| poisoned.into_inner())` instead of
  `.unwrap()`, on the reasoning that a panic *around* the map (a bug in
  the code holding the guard) doesn't mean the `HashMap` itself was left
  structurally broken by a `HashMap` method unwinding mid-mutation.
  `store::tests::write_operations_still_work_after_a_writer_panics`
  deliberately poisons the lock with `panic::catch_unwind` and then
  asserts `Store::create`/`Store::get` still work afterward -- without the
  recovery, that test hangs on nothing and instead panics a second time
  on the poisoned `.unwrap()`.
- **A buffered reader has to stay the same reader for the header line and
  the body, or you lose whatever it already pulled off the socket.**
  `BufReader::read_line` reads in chunks larger than one line and holds
  the rest in its internal buffer; if the body were then read from a
  second, unbuffered handle to the same `TcpStream` (which `try_clone()`
  makes easy to reach for), any body bytes the header read had already
  buffered would simply be gone -- `read_exact` on the raw stream can't
  see into the `BufReader`'s buffer. `Request::parse` (`http.rs`) takes
  `&mut impl BufRead` and reads both the header lines *and* the body
  through that same handle, so nothing buffered during the header read
  can be stranded. I only clone the stream in `server.rs` to get an
  independent writer half -- the read half stays one `BufReader` for the
  whole request.
- **`Content-Length` recomputed at write time, reused from the Java HTTP
  server project earlier this week, held up as a pattern rather than a
  coincidence.** `Response::write` (`http.rs`) drops any
  caller-supplied `content-length` header and always writes
  `body.len()`, the same rule `HttpResponse.write()` uses in
  `2026-08-09-java-http-server`. `http::tests::write_recomputes_content_length_from_the_actual_body`
  deliberately sets a wrong `content-length` via `with_header` first, to
  prove the write path overrides it rather than merely defaulting when
  the header is absent -- the same invariant is worth enforcing the same
  way whether the language is Java or Rust, because the failure mode
  (client trusts a lying length, hangs waiting for bytes that aren't
  coming) doesn't care what wrote the header.

## Deliberate scope cuts

- **No keep-alive.** Every response carries `Connection: close` and the
  socket is dropped right after. The Java HTTP server project already
  covers the keep-alive-loop-with-an-exit-condition lesson in depth; this
  project's lesson is about the shared state behind the handlers, not the
  connection lifecycle in front of them, so duplicating that machinery
  here would just be padding.
- **No connection cap.** `serve()` spawns one thread per accepted
  connection with no bound on how many can be in flight. Fine for a
  learning exercise hit by a handful of clients; wrong for anything
  public, where an attacker opening thousands of slow connections would
  exhaust the thread pool. A bounded worker pool (like the Java project's
  `Executors.newFixedThreadPool`) is the direct fix, deliberately left out
  to keep this project's focus on `Store`'s concurrency correctness.
- **IDs are client-visible sequential integers.** Fine for a learning
  project; a real service would want opaque ids so clients can't infer
  how many records exist or guess at neighboring ones.

## What I'd add next

- **A bounded worker pool**, replacing the unbounded `thread::spawn` per
  connection in `server::serve`, to cap how much concurrency an untrusted
  client population can force onto the process.
- **Optimistic concurrency on `PUT`** (an `If-Match`-style version check),
  since right now two concurrent `PUT`s to the same id just have the
  second one silently win -- correct under the lock, but a client has no
  way to detect it overwrote someone else's write.
- **Chunked or streamed request bodies.** `Request::parse` requires
  `Content-Length` up front, same limitation as the Java project; a large
  upload has to fit in memory as one `Vec<u8>` allocated before the first
  byte is read.
