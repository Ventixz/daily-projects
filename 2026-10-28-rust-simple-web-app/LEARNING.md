# Learning: A Simple Web App in Rust

**Source:** "A Simple Web App in Rust" (Parts 1, 2a, 2b) in the Rust section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Built with the standard library only (no web framework), so the HTTP layer is written by hand.

Run: `cargo run` (listens on `127.0.0.1:8080`; pass another address as the first argument). Test: `cargo test`.

Try it:

```
curl -H 'Accept: application/json' -d 'title=buy milk' localhost:8080/todos
curl localhost:8080/todos
curl -X POST localhost:8080/todos/1/toggle
```

## Layout

- `src/http.rs` — request parser (request line, headers, `Content-Length` body), percent-decoding, form parsing,
  `Response` serialisation.
- `src/store.rs` — in-memory todo list with auto-incrementing ids.
- `src/app.rs` — routing + HTML/JSON rendering. `handle(&mut Store, &Request) -> Response` has no I/O.
- `src/server.rs` — TCP accept loop, one thread per connection, store behind `Arc<Mutex<_>>`.
- Tests: 11 unit tests (parser edge cases, routing, escaping) and 2 integration tests over a real socket
  (end-to-end flow, malformed request, 20 concurrent POSTs).

## What I learned

- **HTTP/1.1 is just text.** A request is a request line, `Key: value` lines, a blank line, then exactly
  `Content-Length` bytes. Reading the body with `read_exact` (not "read until EOF") is what makes keep-it-simple
  `Connection: close` work.
- **Keep the handler pure.** Because `handle` takes a parsed `Request` and returns a `Response`, nearly every behaviour
  is unit-tested without a socket; only two tests need TCP.
- **Shared state in Rust is explicit.** `Arc<Mutex<Store>>` makes the sharing and the locking visible; handling a
  poisoned lock with `into_inner` is a deliberate choice, not an accident.
- **Escape per output context.** The same title needs HTML escaping on the page and JSON escaping in the API; a test
  checks that `<script>` never reaches the page unescaped.
- **Post/Redirect/Get.** Browser form posts answer `303 See Other`, while clients sending `Accept: application/json`
  get a `201` with the created object, from the same route.
- **Limit what you read.** A body size cap (64 KiB) and a read timeout stop one bad client from exhausting memory or
  hanging a thread.

## Known limits

- One thread per connection and `Connection: close` only; no keep-alive, thread pool, or chunked bodies.
- Data lives in memory and is lost on restart.
- The JSON API reads form-encoded bodies, not JSON bodies.
- No auth or CSRF protection; fine for a localhost exercise, not for the internet.
