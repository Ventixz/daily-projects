# Learning: a browser chat in Erlang (hand-written WebSocket server with a long-polling fallback)

**Source:** "Making a Chat App with Erlang, Rebar, Cowboy and Bullet" by Mariano Guerra, from the Erlang section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial uses Cowboy (the Erlang web server) and Bullet, a small library that gives the browser one
"connection" API and quietly uses a WebSocket when it can and long-polling when it can't. marianoguerra.org was
blocked from the build environment, so this follows the tutorial's idea, not its text. It also builds the parts the
libraries hide, **with no dependencies at all**: plain OTP 25 with `gen_tcp`, a hand-written RFC 6455 WebSocket
codec, and a hand-written long-poll transport. No rebar3, no Cowboy, no Bullet.

This is a different project from the earlier [ChatBus](../2026-08-18-erlang-chatbus/) (raw TCP chat for `nc`).
This one is for browsers, and the interesting parts are the HTTP and WebSocket handling and keeping two transports
behind one room.

> Written by the daily-project routine (Claude). Commits are authored as `Claude`, so they are separate
> from the repo owner's own work in the history.

Run: `make run` (or `make run PORT=9000`), then open `http://localhost:8080` in two tabs. Add `?transport=poll`
to one tab's URL to force the long-poll fallback. Both tabs end up in the same room.
Test: `make test`. 34 eunit tests: 10 for the frame codec, 4 for JSON, 6 for the room, and 14 end-to-end tests
over real sockets with a small HTTP client and WebSocket client written in the test file.
Needs Erlang/OTP 25 or newer (`erl`, `erlc`, and the eunit headers; on Debian/Ubuntu that's
`erlang-nox erlang-dev`).

## Layout

- `src/wschat_ws.erl`: the WebSocket protocol, as pure functions only. `accept_key/1` for the handshake,
  `decode/2` (masking, 7/16/64-bit lengths, size limit, protocol errors) and `encode/2`.
- `src/wschat_conn.erl`: one process per TCP connection. It reads an HTTP/1.1 request, routes it, and either
  answers and closes, or upgrades to a WebSocket and stays in `ws_loop`.
- `src/wschat_room.erl`: the room, a `gen_server`. Members are just pids. It handles nick rules, broadcasting,
  bounded history, and announcing leaves through monitors.
- `src/wschat_poll_session.erl`: a long-poll client's stand-in inside the server. It joins the room, buffers
  events, and answers polls.
- `src/wschat_poll_sup.erl`: `simple_one_for_one` supervisor for sessions. It also owns the `sid → pid` ETS table.
- `src/wschat_listener.erl`: the listening socket and the acceptor loop.
- `src/wschat_json.erl`: a 30-line JSON encoder (OTP 25 has no `json` module; OTP 27 does).
- `priv/`: `index.html`, `chat.css`, `chat.js`. The browser client tries the WebSocket first and falls back to
  polling.

Protocol: `GET /ws?nick=ada` upgrades. After that, every text frame from the client is either `/nick NewName` or a
message. The server sends JSON: one `welcome` (history and members), then `join` / `leave` / `msg` / `nick`
events, and `error` replies. Long-poll uses the same events: `POST /poll/join` (body = nick, returns the session
id), then `GET /poll/recv`, `POST /poll/send`, `POST /poll/leave`, with the session id in an `X-Chat-Session`
header.

## What I learned

- **The room shouldn't know about transports.** This is Bullet's whole idea. To the room, a member is a pid that
  receives `{chat_event, Event}`. A WebSocket connection process turns that into a frame straight away. A
  long-poll session process puts it in a buffer until the browser next asks. Because both sides talk to the room
  the same way, a WebSocket user and a polling user can chat with each other, and there is a test for exactly
  that.
- **Long-polling needs something on the server to stand in for the client.** Between polls the browser has no
  connection open, so something has to hold its place in the room and collect what it misses. That's
  `wschat_poll_session`. A poll with events waiting is answered at once. A poll with nothing waiting is *parked*:
  the `gen_server` keeps the caller's `From` and replies later with `gen_server:reply/2` when an event arrives or
  after 25 s. The test checks that a parked poll comes back early once a message arrives, instead of waiting out
  the timeout.
- **Who notices when a polling client disappears? A timer.** A closed tab sends nothing. Each session runs an
  idle timer that only counts while *no* poll is parked (a client sitting in a 25 s poll is clearly still there).
  When the timer fires, the process exits. The room has been monitoring it all along, so the leave is announced
  exactly the same way as when a WebSocket drops.
- **Monitors instead of goodbye messages.** The room calls `erlang:monitor` on every member and treats `'DOWN'`
  as leaving. That covers closed sockets, crashes, expired sessions and heartbeat timeouts with one code path, and
  no member process ever has to remember to say goodbye. Swapping the monitor for a dummy ref makes 5 tests fail.
- **The handshake is a hash, not a password.** `Sec-WebSocket-Accept = base64(sha1(key ++ fixed GUID))`. It doesn't
  authenticate anyone. It only shows that the server really understood the upgrade and isn't a cache or proxy
  echoing headers back. The RFC's worked example (`dGhlIHNhbXBsZSBub25jZQ==` → `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`) is
  the first test.
- **Frames from the client are masked; frames from the server are not.** Every client frame is XORed with a 4-byte
  key. The reason isn't secrecy (the key is sent in the frame). It stops a malicious page from writing bytes that a
  confused proxy might read as an HTTP request and cache. An unmasked client frame is a protocol error (close
  1002). The unmasking works on 32 bits at a time with Erlang binary comprehensions, then handles the 0–3 leftover
  bytes.
- **Erlang can parse HTTP for you, then step out of the way.** `inet:setopts(Sock, [{packet, http_bin}])` makes
  every `recv` return one parsed piece: the request line, then one header at a time, then `http_eoh`. After the
  headers, switching the *same socket* to `{packet, raw}` gives byte-level access for the body or WebSocket frames,
  and anything already buffered carries over. One catch: well-known header names come back as atoms
  (`'Content-Length'`) and others as binaries (`<<"Sec-Websocket-Key">>`, with the casing changed), so everything
  is normalised to lower-case binaries.
- **Check size limits as soon as the header says how big the frame is.** `decode/2` refuses a frame claiming
  1 MB with close 1009 as soon as the length field has arrived, without waiting for (and buffering) the 1 MB. The
  same goes for HTTP: header count, line length (`packet_size`) and `Content-Length` are all capped, and
  `Transfer-Encoding` is refused rather than half-supported.
- **`{active, once}` is flow control.** The WebSocket loop asks for one packet, handles it, then asks again. With
  `{active, true}` a fast sender could fill the process's mailbox faster than it can work through it.
- **WebSockets ignore the same-origin policy, so check `Origin` yourself.** Any web page can open a WebSocket to any
  server, and the browser sends that server's cookies with it (cross-site WebSocket hijacking). If `Origin` is
  present and doesn't match `Host`, the server returns 403. Non-browser clients send no `Origin` and are allowed.
  For long-polling, the session id goes in a custom header rather than the URL. That keeps it out of access logs,
  and a cross-site `fetch` with a custom header needs a CORS preflight, which this server never approves.
- **Half-open TCP connections need a heartbeat.** If a laptop sleeps or a NAT entry expires, no FIN ever arrives,
  and without a heartbeat the user would stay listed as a member forever. The connection sends a ping every
  `ping_interval`. If *nothing* arrives (pong or anything else) before the next tick, it closes with 1001. The test
  uses a client that never replies and gets a ping, then the close.
- **The browser test found two bugs the eunit tests couldn't.** (1) The page's own CSP (`default-src 'self'`)
  blocked its inline `<style>`, so the CSS moved to `/chat.css` and a test now checks the page has no inline
  style or script. (2) `form { display: flex }` overrides the `hidden` attribute, so the join form stayed visible
  after joining. The fix is `[hidden] { display: none !important }`.

## Testing notes

To check that the tests actually test something, I broke the code on purpose: turned off the mask check, the
UTF-8 check, the origin check, the size limit, the heartbeat, case-insensitive nicks, and the room's monitors.
Every one of those changes made at least one test fail.
The JSON key-sorting test found a real bug on its first run. In Erlang term order every atom sorts before every
binary, so sorting a map with mixed key types gave `{"a","c","b"}`. Keys are now converted to binaries first and
sorted after.
A Playwright run (not checked in) opened one tab on the WebSocket and another with `?transport=poll` and checked:
messages both ways, `<b>` is shown as text rather than rendered, `/nick` works, a taken nick over the WebSocket
shows an error *without* falling back to polling, and closing the polling tab produces a `left` line.

## Known limits

- One room. Nicks aren't reserved once their owner leaves, and there's no authentication.
- Long-poll delivery is at-most-once. If the browser's connection drops while a reply is on its way, those events
  are lost. A real implementation would have the client send the last event id it saw (the ids are already there)
  and replay from history.
- HTTP/1.1 only, one request per connection (`Connection: close`), no TLS. Put it behind a reverse proxy for
  `wss://`.
- The server closes its side straight after sending a close frame, instead of waiting for the client's close
  reply. That's allowed, but not the fully polite version of the closing handshake.
- Connection processes aren't supervised. That's the usual choice for short-lived per-socket processes: if one
  crashes, only that client is affected.
