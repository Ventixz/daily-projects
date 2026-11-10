# Learning: HTTP Middleware in Go (stdlib only)

**Source:** "REST Servers in Go, Part 5: middleware" (eli.thegreenplace.net) in the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Scope: my own stdlib-only implementation of a middleware chain: request ID, logging, panic recovery, per-client rate limiting and bearer auth.

Run: `go run .` then `curl -i localhost:8080/health` (`/secret` needs `Authorization: Bearer dev-token`, `/boom` panics).
Test: `go vet ./... && go test -race -v ./...`.

## Layout

- `middleware.go` — `Middleware` type, `Chain`, `RequestID`, `Logging`, `Recover`, `RateLimiter` (token bucket), `BearerAuth`.
- `main.go` — a tiny API (Go 1.22+ `ServeMux` method patterns) wired through the chain.
- `middleware_test.go` — 7 tests using `httptest` and an injected clock.

## What I learned

- **Middleware is just `func(http.Handler) http.Handler`.** Because handlers compose, `Chain` is a short loop, and the first middleware listed is the outermost.
- **Order matters.** RequestID runs first so every later log line has an id; Logging wraps Recover so a panicking request is still logged as a 500; the rate limiter sits inside so rejected requests are also logged.
- **To log a status code you must wrap `ResponseWriter`.** `net/http` gives no way to read it back, so `statusRecorder` intercepts `WriteHeader` (defaulting to 200 when never called).
- **`recover` only works in a deferred function on the same goroutine.** Without it a handler panic drops the connection with no response body.
- **Token buckets need no background goroutine.** Refill lazily on each request from elapsed time, and cap at the burst size.
- **Inject the clock.** Passing `now func() time.Time` made the rate-limit test deterministic, with no sleeps.
- **Compare secrets in constant time** (`subtle.ConstantTimeCompare`) and send `WWW-Authenticate` with a 401.
- **Request-scoped values go in `context`** with an unexported key type to avoid collisions.

## Known limits

- The limiter's bucket map grows without bound (no eviction) and keys on `RemoteAddr`, so clients behind a proxy share a bucket.
- A single static token; no expiry, hashing or per-user identity.
- The status recorder doesn't forward `Flusher`/`Hijacker`, so streaming or websockets through it would break.
