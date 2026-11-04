# Learning: Middleware in Go (REST Servers in Go, Part 5)

**Source:** "REST Servers in Go → Part 5: middleware" (Eli Bendersky) in the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Scope: my own stdlib-only implementation of the ideas (the tutorial itself was not fetched): logging, panic recovery, bearer-token auth and rate limiting as composable middleware.

Run: `go run .` (token `secret`). Test: `go test -v ./...`.

## Layout

- `middleware.go` — `Middleware` type, `Chain`, `Logging`, `Recover`, `BearerAuth`, `RateLimit`, and a demo server.
- `middleware_test.go` — 5 tests: chain order, auth (401 / 200 / `WWW-Authenticate`), panic recovery, status logging, rate limit + window reset.

## What I learned

- **Middleware is just `func(http.Handler) http.Handler`.** Everything composes because `http.Handler` is a one-method interface.
- **Order matters.** `Chain` applies the list so the first entry is outermost: Logging sees everything (including 401s and recovered panics), Recover wraps auth and the handlers, auth runs last before the mux.
- **To observe the status code you must wrap `ResponseWriter`.** A tiny `statusRecorder` overriding `WriteHeader` is enough; default to 200 since handlers that never call it implicitly send 200.
- **`recover()` only works inside a deferred function in the same goroutine**, so Recover cannot protect goroutines a handler spawns.
- **Inject the clock** (`now func() time.Time`) so time-based behaviour like rate limiting is testable without sleeping.
- `httptest.NewRecorder` makes handler tests fast and need no network.

## Known limits

- Rate limiter is global and fixed-window (bursty at window edges); real ones key by client and use token buckets.
- Static token map and no constant-time comparison; fine for a demo, not for production auth.
- The logging test for the server-level chain is weak; the direct `Logging` test is the real check.
