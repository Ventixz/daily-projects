# Building Go Web Applications and Microservices Using Gin (Go)

**Source:** ["Building Go Web Applications and Microservices Using Gin"](https://semaphoreci.com/community/tutorials/building-go-web-applications-and-microservices-using-gin),
from the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial's brief is "build a JSON microservice with Gin: routing,
grouping, middleware, and request binding." I built a bookstore inventory
API rather than the tutorial's own example, specifically because a CRUD
resource with two failure modes that both look like validation errors
(malformed ISBN vs. a duplicate one already in the store) forces the
middleware/handler/domain boundary to actually mean something, instead of
every handler just doing `c.JSON(200, ...)`.

## What it is

- `internal/store/store.go` -- the domain: an in-memory `Store` keyed by
  book ID with a secondary ISBN index, guarded by a single `sync.RWMutex`.
  It knows nothing about HTTP or Gin. `Create`/`Update` reject a duplicate
  ISBN *before* touching the map, and `List` walks IDs `1..nextID` instead
  of ranging the map, so results come back in stable, deterministic order
  despite Go's randomized map iteration.
- `internal/api/handlers.go` -- one method per route. `BookInput` is a
  separate type from `store.Book` specifically so validation tags
  (`binding:"required,isbn"`, `binding:"gt=0"`) live at the HTTP boundary,
  not on the domain type the store persists. `respondStoreError` is the one
  place that maps a domain error to a status code (404 for
  `ErrNotFound`, 409 for `ErrDuplicateISBN`), so that mapping exists exactly
  once instead of once per handler.
- `internal/api/validation.go` -- registers a custom `isbn` binding tag
  with Gin's shared `validator.v10` engine. It checks shape only (10 or 13
  characters, digits with an optional trailing `X` on ISBN-10), not the
  checksum digit, since the point was wiring a custom validator into Gin's
  binding pipeline, not implementing the full ISBN spec.
- `internal/api/middleware.go` -- `RequestLogger` (logs status and latency
  *after* `c.Next()`, so it reports what the handler actually set, not a
  guess) and `RequireAPIKey` (checks `X-API-Key`, aborts with 401 via
  `c.AbortWithStatusJSON` before the handler runs).
- `internal/api/router.go` -- `NewRouter` builds the group tree:
  `/api/v1` → `/books` (open `GET`s) → an inner group with
  `RequireAPIKey` applied only to `POST`/`PUT`/`DELETE`. Reads never see the
  auth middleware; writes can't skip it, because it's attached to the group
  they're registered on, not to each route individually.
- `main.go` -- reads `BOOKSTORE_ADDR`/`BOOKSTORE_API_KEY` from the
  environment, starts the server in a goroutine, and blocks on
  `SIGINT`/`SIGTERM` to call `srv.Shutdown` with a 5-second grace period
  instead of dropping in-flight connections when the process is asked to
  stop.
- `internal/store/store_test.go`, `internal/api/handlers_test.go` -- the
  store tests exercise the domain directly (ID assignment, ISBN uniqueness
  on both create and update, delete freeing an ISBN for reuse); the API
  tests drive the whole router through `httptest.NewRequest` +
  `router.ServeHTTP`, covering the auth gate, the custom validator, and the
  404/409 status mapping end to end.

## Run it

```bash
cd 2026-09-26-go-gin-bookstore
make test                                    # go test ./...
make run                                     # builds and starts on :8080
curl localhost:8080/api/v1/healthz
curl -X POST localhost:8080/api/v1/books \
  -H 'X-API-Key: dev-key' -H 'Content-Type: application/json' \
  -d '{"isbn":"0306406152","title":"GEB","author":"Hofstadter","price":24.5}'
```

## What it actually teaches

- **A route group is where middleware actually lives, not a per-route
  decision.** `RequireAPIKey` is registered once, on `protected :=
  books.Group("")`, and every route added to `protected` afterward inherits
  it automatically. Adding a new write endpoint later means adding it to
  the right group; there's no `.Use(RequireAPIKey(...))` call to remember
  to copy onto each new route, and no way to accidentally register a write
  route on the open group by forgetting one line.
- **Logging middleware has to run its logic *after* `c.Next()`, not
  before.** `RequestLogger` calls `c.Next()` first and reads
  `c.Writer.Status()` afterward. A version that logged before `c.Next()`
  would only ever be able to log the request, never the response --
  Gin hasn't picked a status code until a handler further down the chain
  sets one.
- **Registering a custom validator is a one-time, process-wide side
  effect, not a per-request one.** `binding.Validator.Engine()` returns the
  same `*validator.Validate` instance Gin uses for every `ShouldBindJSON`
  call in the process. `RegisterValidators` has to run once, before the
  first request, because calling `RegisterValidation` inside a handler
  would work but would silently re-register the same tag on every request
  for no benefit -- and in a concurrent server, would be a data race on the
  validator's internal tag map.
- **Two errors that look identical from outside the store need to stay
  distinguishable past the HTTP boundary.** `ErrNotFound` and
  `ErrDuplicateISBN` both mean "the request was rejected," but they're
  different sentinel errors so `respondStoreError`'s `errors.Is` switch can
  send back 404 vs. 409. Collapsing them into one generic "invalid
  request" error in the store would still pass a smoke test that only
  checks "is the create rejected" but would break a client that needs to
  tell "this book doesn't exist yet" apart from "you already have one with
  this ISBN."
- **`httptest.NewRequest` plus `router.ServeHTTP` tests the whole stack,
  middleware included, without a real listening socket.** Every test in
  `handlers_test.go` goes through `RequireAPIKey` and the custom ISBN
  validator exactly as a live server would -- there's no separate
  "unit test the handler function directly" path that could drift from
  what the router actually wires up in production.

## Deliberate scope cuts

- **No persistence.** `Store` is an in-memory map for the process's
  lifetime; the tutorial is about Gin's routing and middleware model, not
  a storage layer, so there's no database or file backing.
- **No pagination or filtering on `List`.** It returns every book in one
  response; a real inventory API would need `?limit=`/`?cursor=`, but nothing
  here needed more than a handful of books to exercise the routes.
- **One shared API key, not per-client credentials.** `RequireAPIKey`
  compares against a single configured string. Real auth (per-client keys,
  expiry, scopes) belongs in a project about auth specifically, not one
  about Gin's middleware mechanics.
- **ISBN validation checks shape, not the check digit.** Verifying the
  ISBN-10 modulo-11 or ISBN-13 modulo-10 checksum would make
  `validateISBN` a checksum-algorithm exercise instead of a
  "wire a custom tag into Gin's validator" exercise.

## What I'd add next

- **A `?q=` search endpoint** on `/books` filtering by title/author
  substring, since `List`'s only variation right now is "all of them."
- **Structured logging** (swap the `log.Printf` line in `RequestLogger` for
  `slog` with request-scoped fields) so the log output could actually be
  queried in something like Loki instead of grepped by eye.
- **An OpenAPI spec generated from the route table**, so the contract this
  project already enforces at runtime (via binding tags) is also
  documented somewhere a client developer would look first.
