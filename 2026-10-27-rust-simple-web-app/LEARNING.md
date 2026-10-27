# Learning: A Simple Web App in Rust

**Source:** "A Simple Web App in Rust" (Joel McCracken) in the Rust section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Built with the standard library only (no web framework, no crates), so every layer of HTTP is written by hand.

Run: `cargo run` then open <http://127.0.0.1:8080> (optional first argument sets the address). Test: `cargo test`.

## Layout

- `src/lib.rs` — request parser, `Response` serialiser, form/percent decoding, HTML/JSON escaping, `Store`, `route`.
- `src/main.rs` — `TcpListener` accept loop, one thread per connection, shared `Arc<Mutex<Store>>`.
- `tests/e2e.rs` — real TCP round trips against an ephemeral-port server, including 8 concurrent clients.
- 9 unit tests: request parsing, malformed/oversized rejection, percent decoding, form parsing, the add/toggle/delete
  flow, validation and 404/405 paths, HTML escaping, JSON API, response bytes.

## What I learned

- **HTTP/1.1 is just text over TCP.** A request is a start line, `Name: value` headers, a blank line, then exactly
  `Content-Length` bytes. A response is the same shape. `Connection: close` avoids having to implement keep-alive.
- **Make `route` a pure function** of `(&mut Store, &Request) -> Response`. Handlers then test without any sockets,
  and the socket code shrinks to read, route, write.
- **Post/Redirect/Get.** Form posts answer `303 See Other` to `/`, so refreshing the page never re-submits the form.
- **Escape on output.** User text is HTML-escaped when rendered and JSON-escaped in the API; a test proves `<script>`
  never appears verbatim.
- **Shared state in Rust.** `Arc<Mutex<Store>>` lets each connection thread mutate the same list; the compiler refuses
  unsynchronised access, and the concurrency test shows no lost updates.
- **Parse defensively.** Bad request lines, truncated bodies and bodies over 64 KiB return `None` (→ 400) instead of
  panicking or allocating whatever a client claims.

## Known limits

- State is in memory and lost on restart; ids are never reused.
- One thread per connection with no timeouts, so a slow client holds a thread (slowloris-able). Not for the public internet.
- No keep-alive, chunked bodies, HTTPS, CSRF protection, or query-string parsing beyond ignoring it.
- Single-segment routing is a `match` on path segments, not a real router.
