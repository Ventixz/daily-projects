# URL Shortener (Go)

**Source:** [Go → URL Shortener](https://www.eddywm.com/lets-build-a-url-shortener-in-go/), from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting rather than scaffolded-then-attempted, so this folder
contains the finished implementation directly at the project root (no separate `reference/`).

## What it is

A small HTTP service, written against only Go's standard library, that takes a long URL and hands
back a short one. `POST /api/shorten` mints a code, `GET /{code}` looks it up and 302-redirects,
`GET /api/stats/{code}` reports click counts, and a single-page vanilla-JS UI at `/` wraps all of
that in a form and a table. Links persist to a JSON file, so a restart doesn't lose them.

## Run it

```powershell
# Windows 11 / PowerShell
winget install GoLang.Go
go version   # confirm the toolchain installed

cd 2026-07-15-go-url-shortener
go run .                      # listens on :8080, persists to data/links.json
```

```bash
# macOS/Linux
go run .
```

Then either open `http://localhost:8080/` in a browser, or drive it directly:

```bash
curl -X POST localhost:8080/api/shorten -d '{"url":"https://go.dev/doc/effective_go"}'
# {"code":"qz5QAu","url":"...","short_url":"http://localhost:8080/qz5QAu"}
curl -i localhost:8080/qz5QAu   # 302 -> Location: https://go.dev/doc/effective_go
```

Run the test suite with `go test ./...`.

## What it actually teaches

- **Go 1.22+'s enhanced `net/http` router.** Method-specific patterns (`"GET /{code}"`,
  `"POST /api/shorten"`) and the `{$}` exact-match pattern let you get real REST routing —
  including wildcard path segments read back via `r.PathValue("code")` — without reaching for
  a third-party router like `chi` or `gorilla/mux`. Getting the routing table right (exact `/`
  vs. the wildcard `/{code}` catch-all) is the part that actually trips people up here: register
  the exact match first or the wildcard swallows it.
- **Safe concurrent shared state.** Every HTTP request runs on its own goroutine, and they all
  hit the same in-memory map of links. `sync.RWMutex` (readers for lookups/redirects, a full
  lock for writes) is the standard shape for "many readers, occasional writer" state — the
  alternative, one global lock for everything, would serialize every redirect behind every other
  request.
- **Crash-safe persistence.** Writing JSON straight to `data/links.json` risks a half-written
  file if the process dies mid-write. Writing to `links.json.tmp` and `os.Rename`-ing it into
  place is atomic on POSIX filesystems, so the store is never observed in a partially-written
  state — the same pattern databases and package managers use for "safe write."
- **Why URL validation is a security boundary, not a formality.** A short-link redirector is
  attractive to abuse specifically because it silently forwards whatever you hand it. Requiring
  `url.ParseRequestURI` to succeed *and* the scheme to be `http`/`https` blocks `javascript:`
  URIs and bare `example.com` (which browsers would otherwise treat as a relative path from the
  shortener's own origin) before they ever reach `http.Redirect`.

## What I'd add next (stretch goals I skipped for scope)

- Rate limiting on `/api/shorten` — right now anyone can mint unlimited codes.
- Expiring links (a `TTL` field checked at redirect time).
- Swapping the JSON file for SQLite once link volume would make a full-file rewrite on every
  click too slow — the `Store` interface is already the seam where that would slot in.
