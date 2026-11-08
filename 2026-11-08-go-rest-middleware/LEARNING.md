# Learning: REST Servers in Go — Part 5: Middleware (Go)

**Source:** "REST Servers in Go, Part 5 - middleware" (Eli Bendersky) in the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Scope: standard-library-only middleware toolkit and a tiny demo app. My own implementation of the idea, not a copy of the tutorial.

Run: `go run .` then `curl -H 'Authorization: Bearer s3cret' localhost:8080/whoami`. Test: `go test -race ./...`.

## Layout

- `middleware.go` — `Middleware` type, `Chain`, and five middlewares: `RequestID`, `Logging`, `Recover`, `RateLimit` (token bucket per client IP), `BearerAuth`.
- `main.go` — `NewHandler` wires a mux (`/health` public, `/whoami` authenticated, `/boom` panics) and applies the chain.
- `middleware_test.go` — chain order, request ID + log line, auth failure modes, panic recovery, rate-limit exhaustion and refill using an injected clock.

## What I learned

- **Middleware is just `func(http.Handler) http.Handler`.** `Chain` applies them in reverse so the first one listed is outermost.
- **Order matters.** RequestID goes first so every later layer (including logs) can see the ID; Logging sits outside Recover so a panic still gets logged as a 500.
- **Wrapping `ResponseWriter` to capture the status** needs a small struct embedding the interface and overriding `WriteHeader`. Handlers that never call it default to 200.
- **Inject the clock** (`now func() time.Time`) to test the rate limiter deterministically, with no sleeps.
- **Rate-limit by IP, not `RemoteAddr`**: the port differs per connection, so `net.SplitHostPort` is required (a test uses two ports from one IP).
- **Context keys should be a private type** to avoid collisions; accessor helpers hide the type assertion.

## Known limits

- The rate limiter never evicts idle buckets (memory grows with distinct IPs) and ignores `X-Forwarded-For`.
- Tokens are a hard-coded in-memory map and compared with a normal map lookup, not constant-time.
- The wrapped `ResponseWriter` drops optional interfaces (`Flusher`, `Hijacker`).
- Per-route middleware is only demonstrated for auth; no CORS or timeouts.
