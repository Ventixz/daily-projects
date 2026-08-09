# Build a Simple HTTP Server (Java)

**Source:** ["Build a Simple HTTP Server with Java"](http://javarevisited.blogspot.com/2015/06/how-to-create-http-server-in-java-serversocket-example.html),
from the Java section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial's brief is a `ServerSocket` that answers "hello" to any GET. I
kept the "raw socket, no framework" constraint -- no `com.sun.net.httpserver`,
no Jetty -- but built out HTTP/1.1 request parsing, exact-path routing with
correct 404/405 semantics, keep-alive connections, and a path-traversal-safe
static file server, all written from my own understanding of the protocol
rather than transcribed from the tutorial's single-file example.

## What it is

- `src/HttpRequest.java` -- parses a request straight off the socket's
  `InputStream`: request line, headers (case-insensitive, repeated headers
  merged), query string, and a `Content-Length`-bounded body read as raw
  bytes.
- `src/HttpResponse.java` -- status line, headers, body; `Content-Length` is
  always computed from the actual body array at write time, never trusted
  from a caller.
- `src/Router.java` -- a two-level table (path, then method) so a path that
  exists but doesn't support the request's method answers 405, not 404.
- `src/StaticFileHandler.java` -- serves a directory tree with a
  resolve-then-verify traversal guard (see below) and per-extension
  `Content-Type`.
- `src/HttpServer.java` -- the accept loop and the per-connection worker:
  a fixed thread pool, one worker per open socket, looping over keep-alive
  requests until the client closes, sends `Connection: close`, or an idle
  timeout fires.
- `tests/` -- 39 hand-rolled assertions (no JUnit, no Maven, matching this
  repo's other Java project): request parsing, router 404-vs-405, static
  file traversal, and four end-to-end tests that open a real `Socket`
  against a running `HttpServer` instance rather than mocking any of it.

## Run it

```bash
cd 2026-08-09-java-http-server
make test                 # 39 assertions, no dependencies to install
make run                  # serves ./public on :8080
make run PORT=9000 ROOT=public
curl -i http://localhost:8080/
curl -i http://localhost:8080/api/time
curl -i "http://localhost:8080/api/echo?msg=hello"
```

## What it actually teaches

- **A 404-vs-405 distinction needs two lookups, not one.** It's tempting to
  key a routing table by `"GET /hi"` as a single combined string, but then a
  `POST /hi` with no matching key looks identical to a request for a path
  that was never registered at all -- both just fail the lookup. `Router`
  keys by path first, into a `Map<String, Handler>` of methods second
  (`Router.java`), so `route()` can tell "this path doesn't exist" (404)
  apart from "this path exists, wrong method" (405, with an `Allow` header
  built from the *other* registered methods) as two different branches, not
  one. `RouterTests.testWrongMethodOnKnownPathIs405WithAllowHeader` pins the
  case a single flat map can't distinguish.
- **A traversal guard has to check the destination, not the request
  string.** My first instinct was to reject any request path containing
  `".."`. That's the wrong shape of check -- it's a blocklist against one
  spelling of the attack (what about `%2e%2e`, or a symlink?). `
  StaticFileHandler.handle` instead resolves the request against the root
  directory and calls `.normalize()`, which collapses every `..` and `.`
  segment mechanically, then checks `candidate.startsWith(root)` on the
  *result*. It doesn't matter how the traversal was spelled in the request;
  what matters is where it lands. `StaticFileHandlerTests
  .testTraversalOutsideRootIs403` proves a file that genuinely exists one
  directory above root is still unreachable through `/../secret.txt`.
- **Path and query decoding are not the same operation, even though both
  are "percent decoding."** In a query string, `+` means space (that's
  `application/x-www-form-urlencoded`, not HTTP itself) -- `q=hello+world`
  has to become `"hello world"`. In the path, `+` is a literal character;
  `/a+b/%41` has to become `/a+b/A`, not `/a b/A`. `HttpRequest` has two
  separate decoders (`percentDecodePath` and the query-string `urlDecode`)
  for exactly this reason -- collapsing them into one would either break
  literal `+` in filenames or leave `+` un-decoded in form data.
  `HttpRequestParserTests.testPathPercentDecodingDoesNotTouchPlus` and
  `testQueryStringDecoding` cover the two directions independently so a
  future edit that "simplifies" them into one function fails immediately.
- **Content-Length has to be a property of the response object at send
  time, not a header the caller sets by hand.** I started writing a HEAD
  handler that set `Content-Length` to the real file size while leaving
  `body` empty (no bytes to send, but the client still needs to know how
  big the file *would* be). `HttpResponse.write()` always overwrites any
  `Content-Length` header with `body.length` before sending, which is
  correct for every GET/POST response in this server but is silently wrong
  for HEAD -- the header would report `0` no matter what I set it to. I
  didn't want a response type whose correctness depends on the caller
  remembering an exception to the write path's own invariant, so I dropped
  HEAD entirely rather than special-case the writer. Recognizing that
  conflict *before* shipping it, from just reading `write()` next to the
  handler I'd drafted, was the actual lesson -- catching a header/body
  mismatch by inspection is a lot cheaper than debugging it from a client
  that trusts a lying Content-Length.
- **Keep-alive is a loop with an exit condition checked per-request, not a
  socket-level setting.** `handleConnection` re-parses a new
  `HttpRequest` off the same `InputStream` in a `while (true)` after every
  response, and decides whether to loop again from `shouldKeepAlive`
  (`HttpServer.java`): HTTP/1.1 defaults to persistent unless the request
  says `Connection: close`; HTTP/1.0 is the opposite default. Getting this
  backwards silently turns every response into either a hung connection
  (client waits for more bytes that never come) or a wasted TCP handshake
  per request. `EndToEndTests.testTwoRequestsReuseOneKeepAliveConnection`
  sends two full requests down one `Socket` object and only passes if the
  server genuinely kept reading from the same connection instead of closing
  after the first response.
- **A thread-per-connection idle keep-alive socket has to time out, or the
  thread pool eventually starves.** Without `socket.setSoTimeout(...)`,
  a client that opens a keep-alive connection and never sends a second
  request holds its worker thread blocked on `in.read()` forever --
  harmless for one client, fatal for a fixed-size pool once enough clients
  do it. `handleConnection` catches `SocketTimeoutException` from the *next*
  request's read (not the first, which the client is expected to send
  immediately) and just closes quietly, exactly as if the client had hung
  up. This is also why `HttpRequest.parse` returning `null` versus throwing
  `HttpParseException` matters: a clean EOF between requests (client done)
  and a timeout (client stalled) both end the loop the same way, but a
  genuinely malformed request still gets a `400` before the socket closes.

## What I'd add next (stretch goals skipped for scope)

- **Chunked transfer encoding**, both directions -- currently a request
  body's size has to be known up front via `Content-Length`; there's no
  `Transfer-Encoding: chunked` support for streamed bodies.
- **TLS.** This is plaintext HTTP only; wrapping the `ServerSocket` in an
  `SSLServerSocket` for HTTPS is a config change, not a rewrite, since all
  the parsing/routing works against `InputStream`/`OutputStream`.
- **A slow-loris hard timeout on the *first* line of a request**, not just
  between requests. `IDLE_TIMEOUT_MS` bounds the wait for a next request on
  an already-open connection, but a client that opens a connection and
  trickles a request line one byte per second is only bounded by the same
  15s socket timeout per read, not a wall-clock deadline on the whole
  request.
- **More HTTP methods.** Only GET/POST are used by anything in this project
  (`StaticFileHandler` explicitly 405s everything but GET); PUT/DELETE would
  reuse the exact same `Router`/`Handler` machinery.
