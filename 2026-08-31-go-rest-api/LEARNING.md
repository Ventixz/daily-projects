# Learning: REST Servers in Go — Part 1 (Standard Library)

## What it is

- `internal/store/store.go` — `Store`, an in-memory `map[int]Book` guarded
  by a `sync.RWMutex`. Every book carries a `Version int` that starts at 1
  and increments on every successful `Update`. `List` takes a `ListFilter`
  (author substring, offset, limit) and returns both the paginated slice
  and the total match count *before* pagination, so a caller can build
  `X-Total-Count` without a second unpaginated query.
- `internal/api/api.go` — `NewServer(*store.Store) http.Handler` builds a
  plain `http.ServeMux` using Go 1.22's method-and-path patterns
  (`"PUT /books/{id}"`), wraps it in `recoverMiddleware(loggingMiddleware(mux))`,
  and returns that. No router package, no framework — just what shipped in
  the standard library.
- Optimistic concurrency: `create` sets `ETag: "v1"`; `update` requires an
  `If-Match` header, rejects a missing one with `428`, a mismatched one
  with `412`, and only then touches the store.
- `internal/store/store_test.go` — 7 unit tests, including
  `TestConcurrentAccess`, which fires 50 goroutines that each create a
  book and then loop `Get`→`Update` until their own write lands, run under
  `go test -race`.
- `internal/api/api_test.go` — 9 integration tests driving the whole
  handler chain through `httptest.NewRecorder`, covering every route,
  every status code the API can return, and a direct test of
  `recoverMiddleware` against a handler that panics on purpose.

## Run it

```bash
cd 2026-08-31-go-rest-api
make test    # 16 checks across store and api packages
make race    # same suite, -race, to actually prove the mutex is doing its job
make run     # starts the API on :8080, seeded with two books
```

## What it actually teaches

- **`ETag`/`If-Match` moves the concurrency check from "when I locked the
  row" to "when the client last read the resource."** There's no lock
  held across a request boundary — `update()` in `api.go` does a plain
  `Get`, and the version it read travels back to the client in the `ETag`
  header. The *next* write from that client has to present that exact
  version back as `If-Match`, and `store.Update` re-checks it atomically
  against whatever the version actually is at write time. Two clients
  racing to `PUT` the same book: one wins, the loser's `store.Update`
  returns `ErrVersionMismatch` even though its own `Get` succeeded
  moments earlier — `TestUpdateRequiresIfMatch`'s last assertion is
  exactly this, reusing an `ETag` that a prior call in the same test just
  made stale.
- **`428` and `412` are different failures and collapsing them into `400`
  would lose information a client needs.** `428 Precondition Required`
  means "you didn't even try to tell me what version you had" (send
  `If-Match` next time); `412 Precondition Failed` means "you told me,
  and you were wrong" (re-`GET`, then retry). A client can distinguish
  "I forgot a header" from "I raced someone" only if the server keeps
  those codes apart — `update()` checks `ifMatch == ""` before it checks
  `ifMatch != etag(...)`, on purpose, in that order.
- **`go test -race` is not decoration — it changes what "correct" means
  for `TestConcurrentAccess`.** Without `-race`, a data race on the
  underlying map would likely just... pass, silently, most of the time,
  because Go maps rarely crash cleanly on concurrent access in a short
  test run; they corrupt or panic unpredictably. `make race` is a
  separate Makefile target specifically so this suite gets run *with*
  the detector, not just under plain `go test`, where the guarantee
  `sync.RWMutex` is supposed to provide would otherwise go unchecked.
- **The total count for pagination has to be computed before slicing, not
  after.** `store.List` builds `matched` from the filter, records
  `total := len(matched)`, and only *then* slices by offset/limit. Doing
  it in the other order — slicing first and taking `len()` of the page —
  would report the page size as the total, and `X-Total-Count` would lie
  the moment a client asked for `limit=1`. `TestListBooksFilterAndPagination`
  checks the header equals `3` on a `limit=1&offset=1` request specifically
  to pin this down.

## Deliberate scope cuts

- **No persistence.** The store is a map that resets on restart; this
  project is about the HTTP/concurrency-control layer, not storage
  engines (that's closer to what the C "Let's Build a Simple Database"
  and TypeScript "Simple Database" days already covered).
- **No partial updates (`PATCH`).** `PUT` requires the full resource body
  every time; there's no merge-patch semantics for updating just one
  field.
- **Author filtering is a single case-insensitive substring match, not a
  query language.** No AND/OR of multiple fields, no range filters on
  `year`.
- **No auth.** Every request can read and write every book; this project
  is scoped to REST mechanics (status codes, concurrency control,
  pagination), not access control.

## What I'd add next

- **A `PATCH` endpoint** using JSON Merge Patch (RFC 7396) semantics,
  still gated by the same `If-Match` check `PUT` uses, to see whether the
  optimistic-concurrency code in `update()` generalizes cleanly to a
  "some fields" write or needs to be duplicated.
- **A persistent backend** (even just a single BoltDB/SQLite file) behind
  the same `Store` interface, to check whether `ListFilter`'s shape
  survives moving off an in-memory map — offset/limit pagination gets
  much more expensive on a real table without an index.
- **Structured request-scoped logging** (a request ID threaded through
  `context.Context`) instead of `loggingMiddleware`'s one line per
  request, so a failure could be traced across multiple log lines instead
  of inferred from adjacent timestamps.
