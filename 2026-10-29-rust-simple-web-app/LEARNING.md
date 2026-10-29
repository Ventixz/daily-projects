# Learning: A Simple Web App in Rust

**Source:** "A Simple Web App in Rust" (Part 1) in the Rust section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Note: the tutorial site was unreachable from the build environment, so this is built from the title alone (a std-only
web app), not by following the tutorial's steps or crates.

Run: `cargo run` (or `cargo run -- 127.0.0.1:3000`), then open http://127.0.0.1:8080. Test: `cargo test`.

## Layout

- `src/http.rs` — request parser (request line, headers, `Content-Length` body), percent/form decoding, `Response` builder.
- `src/app.rs` — the app: an in-memory notes board with `GET /`, `POST /notes`, `POST /notes/delete`, `GET /health`.
- `src/lib.rs` — `serve_connection`: parse, dispatch under a `Mutex<App>`, write the response.
- `src/main.rs` — `TcpListener` accept loop, one thread per connection.
- `tests/web.rs` — 10 tests: decoding, parsing, malformed input, add/delete, HTML escaping, routing, framing, and a real TCP round trip.

## What I learned

- **HTTP is just text over a socket.** A request is a line, headers, a blank line, then exactly `Content-Length` bytes.
- **Keep the handler pure.** `App::handle(&Request) -> Response` needs no sockets, so almost everything is unit-testable.
- **Post/Redirect/Get.** A 303 after a successful POST stops a browser refresh from re-submitting the form.
- **Escape on output.** Notes are user input; `html_escape` prevents stored XSS (covered by a test).
- **Shared state in Rust.** `Arc<Mutex<App>>` makes the threading explicit; the compiler forces it.

## Known limits

- One thread per connection, no keep-alive, no thread pool; a slow client holds a thread (5 s read timeout).
- Notes live in memory and vanish on restart; no CSRF protection.
- No path-parameter routing or HTTP/1.1 features beyond the basics (chunked bodies, `Expect: 100-continue`).
