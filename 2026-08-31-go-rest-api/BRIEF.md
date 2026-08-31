# REST Servers in Go — Part 1: Standard Library

**Source:** ["REST Servers in Go - Part 1 - standard
library"](https://eli.thegreenplace.net/2021/rest-servers-in-go-part-1-standard-library/),
from the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

## What to build

A REST API for a small book collection, on Go's `net/http` alone — no
router package, no framework:

- `POST /books`, `GET /books`, `GET /books/{id}`, `PUT /books/{id}`,
  `DELETE /books/{id}`, using Go 1.22+'s method-aware `ServeMux` patterns
  (`mux.HandleFunc("GET /books/{id}", ...)`).
- An in-memory store, safe for concurrent access.
- Optimistic concurrency control on `PUT`: every book carries a version,
  exposed as an `ETag`. A client must send `If-Match` with the version it
  last read; a missing header is `428 Precondition Required`, a stale one
  is `412 Precondition Failed`.
- Consistent JSON error bodies and proper status codes throughout (`201`
  with `Location`, `204` on delete, `400` on bad input, `404` on missing
  resources).
- Two hand-rolled middlewares: request logging and panic recovery.

## What it teaches

- The stdlib `net/http.ServeMux` has been a real router since Go 1.22 —
  method + path-parameter patterns (`"PUT /books/{id}"`) without pulling
  in gorilla/mux, chi, or gin.
- Optimistic concurrency (`ETag`/`If-Match`) is what a REST API needs
  instead of a lock spanning a read and a later write — the version check
  happens at write time, not read time, and the client is the one who has
  to prove it saw the latest state.
- `net/http/httptest` tests a full handler chain — routing, middleware,
  JSON encoding — without opening a real socket.
- `go test -race` is how a concurrency claim ("the store is safe for
  simultaneous requests") gets checked rather than assumed.

## Setup

- Plain Go, standard library only. `go build ./...` / `go test ./...`
  need nothing beyond the Go toolchain — no `go.sum`, no third-party
  modules.

## Milestones

1. `internal/store`: an in-memory `map[int]Book` behind a `sync.RWMutex`,
   with `Create`/`Get`/`List`/`Update`/`Delete`, versioned for optimistic
   concurrency. Unit tests, including one driving `Create`/`Get`/`Update`
   from many goroutines at once under `-race`.
2. `internal/api`: handlers for all five routes, wired through
   `http.ServeMux`'s method+path patterns.
3. Validation and status codes: `400` on missing fields or malformed JSON,
   `404` on an unknown id, `201`+`Location`+`ETag` on create, `204` on
   delete.
4. `If-Match`-gated `PUT`: `428` with no header, `412` on a stale one,
   `200` with a fresh `ETag` on success.
5. Logging and panic-recovery middleware, composed around the mux.
6. An `httptest`-driven integration suite covering the full request/response
   cycle for every route, plus the concurrency-control edge cases.
