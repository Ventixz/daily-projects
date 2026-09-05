# Learning: Rust in Detail — Writing a Scalable Chat Service from Scratch (Rust)

**Source:** ["Rust in Detail: Writing Scalable Chat Service from Scratch"](https://nbaksalyar.github.io/2015/07/10/writing-chat-in-rust.html)
(Part 1, WebSocket introduction), from the Rust section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Picked and built end-to-end in one sitting, so this folder contains the finished implementation
directly at the project root (no separate `reference/`).

## What it is

A WebSocket chat server with the RFC 6455 handshake and framing hand-rolled from scratch —
`std` only, no crates — plus a broadcast core that fans messages out to every other connected
client:

- `src/sha1.rs` — a from-scratch SHA-1 (RFC 3174), needed only because the WebSocket handshake's
  `Sec-WebSocket-Accept` header is `base64(sha1(client_key + magic_guid))`.
- `src/base64.rs` — just enough base64 encoding (standard alphabet, `=` padding) to turn that
  digest into the header value.
- `src/websocket.rs` — the actual protocol: pulling `Sec-WebSocket-Key` out of the raw HTTP
  upgrade request, computing the accept key, and decoding/encoding RFC 6455 frames (length forms,
  client-side masking, opcodes).
- `src/server.rs` — one OS thread per connection, a `Mutex<HashMap<id, TcpStream>>` client
  registry, and `broadcast`, which writes a text frame to every client except the sender and
  drops any peer a write fails against.
- `src/main.rs` — binds `127.0.0.1:9001` (or an address from `argv[1]`) and serves forever.

## Run it

```bash
cd 2026-09-05-rust-websocket-chat
cargo test    # 14 tests: unit (SHA-1, base64, framing) + real-socket integration
cargo run --release -- 127.0.0.1:9001
```

Then, from a browser console pointed at that address:

```js
let ws = new WebSocket("ws://127.0.0.1:9001");
ws.onmessage = (e) => console.log("received:", e.data);
ws.onopen = () => ws.send("hello from the browser");
```

Open two tabs: each one's message shows up in the *other* tab's console, never its own — that's
`broadcast`'s sender-exclusion working over an actual browser WebSocket, not just the test suite's
raw sockets.

## What it actually teaches

- **The handshake is the whole trust boundary, and it's just string processing.** Upgrading from
  HTTP to WebSocket is one `Sec-WebSocket-Accept` header proving the server actually read the
  client's `Sec-WebSocket-Key` — `sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")`, base64'd.
  There's no cryptographic secret here (the magic GUID is public, fixed by the RFC); it exists
  purely to stop a misconfigured HTTP server or cache from accidentally answering a WebSocket
  handshake it doesn't understand. `test_websocket.rs::accept_key_matches_rfc6455_worked_example`
  pins this against the RFC's own worked example (`dGhlIHNhbXBsZSBub25jZQ==` →
  `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`) — an easy value to get wrong quietly, since a subtly incorrect
  SHA-1 or base64 still produces *a* 28-character string that looks plausible.
- **Client and server frames are asymmetric on purpose, and mixing that up fails silently.**
  RFC 6455 requires every client→server frame to be masked (a 4-byte XOR key inline in the frame)
  and forbids the server from masking anything it sends back. `read_frame` unmasks
  unconditionally when the mask bit is set and never masks when it isn't — it doesn't special-case
  "client" vs. "server," it just does what the mask bit says, which is what makes the same
  function correct for decoding both the client's masked frames in `server.rs` and the server's
  own unmasked frames in `test_websocket.rs::decodes_rfc6455_unmasked_server_frame`. Getting this
  backwards (masking server frames, or forgetting to unmask client ones) doesn't error — it just
  hands the application garbled bytes that happen to still be the right *length*.
- **The 7-bit length field is a tagged union, not a real length.** 0–125 is a literal length;
  126 and 127 aren't lengths at all, they're sentinels saying "read 2 more bytes" / "read 8 more
  bytes" for the real length. `encode_then_decode_round_trips_for_longer_payloads` sweeps lengths
  on both sides of 125 and 0xFFFF specifically because those are the only two places the decoder's
  branch logic can be wrong — everything strictly between them exercises the exact same code path.
- **A `Mutex<HashMap<Id, TcpStream>>` registry makes "broadcast" and "someone disconnected" the
  same problem.** `broadcast` doesn't distinguish a client that's merely slow from one that's
  gone — a failed `write_all` is the only signal it gets, so it removes that id from the registry
  right there instead of waiting for that client's own reader thread to notice the same thing.
  `test_server.rs::disconnected_client_is_dropped_and_does_not_break_broadcast` drops a client
  mid-session and then proves broadcast still works cleanly afterward, which is the case a naive
  "unwrap the write" implementation gets wrong by panicking a thread instead of just forgetting
  that one dead peer.
- **Excluding the sender from its own broadcast is a one-line filter, but skipping it is the kind
  of bug a single-client demo never catches.** `broadcast` skips `sender_id` while iterating the
  registry; `test_server.rs::broadcasts_to_others_but_not_back_to_sender` is what actually forces
  that line to exist — with only one client connected (the common way to smoke-test a chat
  server by hand), a version that echoes to everyone looks identical to a version that doesn't.

## Deliberate scope cuts

- **No message fragmentation.** Every frame this server reads or writes has `FIN=1`; a real
  client splitting one logical message across multiple frames (rare for chat-sized text, common
  for large binary payloads) isn't handled.
- **No `wss://` / TLS.** Plain-text sockets only, matching the tutorial's scope.
- **No permessage-deflate or any other extension negotiation.** The handshake accepts whatever
  extensions the client asks for and simply doesn't offer any back.
- **No room/channel concept.** Every connected client is in one global broadcast group; there's
  no way to address a subset of clients.
- **No backpressure on a slow reader.** `broadcast` calls a blocking `write_all` per client while
  holding the registry lock; a client that never drains its socket buffer would stall every other
  client's messages, not just its own. Fine for a demo, not for production traffic.
- **Close handshake is one-directional.** The server stops on an incoming `Close` frame but never
  sends its own `Close` frame back before dropping the connection, which is a protocol violation
  strict clients may complain about (browsers tolerate it).

## What I'd add next

- **A proper close handshake** — echo a `Close` frame back before closing the socket, and use the
  close code/reason payload instead of just hanging up.
- **Ping/pong keepalive from the server side** — right now the server only *answers* pings; a
  production chat service would also periodically ping idle clients to detect half-open
  connections (a peer whose TCP connection died without a clean FIN).
- **Bounded per-client outbound queues** (a channel + a dedicated writer thread per client)
  instead of writing directly from `broadcast` while holding the registry lock, so one slow
  client can't stall delivery to everyone else.
- **Rooms**, by keying the registry on `(room_id, client_id)` instead of a single flat map.
