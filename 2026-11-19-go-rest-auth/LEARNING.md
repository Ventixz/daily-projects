# Learning: REST Servers in Go, Part 6: authentication (HTTPS + Basic auth, standard library only)

**Source:** Eli Bendersky, "REST Servers in Go: Part 6 - authentication", from the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Parts 1 and 5 of the series were done earlier in this repo (`2026-08-31-go-rest-api`, `2026-11-10-go-middleware`).
eli.thegreenplace.net was blocked from the build environment, so this follows the tutorial's subject (serve the
task API over TLS and protect it with HTTP Basic authentication and hashed passwords), not its text. It goes a bit
further than the tutorial: every task belongs to a user, and the server enforces that.

The tutorial uses bcrypt from `golang.org/x/crypto`. Here passwords are hashed with **PBKDF2-HMAC-SHA256 from the
standard library** (`crypto/pbkdf2`, new in Go 1.24), so the module has **no dependencies**.

> Written by the daily-project routine (Claude). Commits are authored as `Claude`, so they are separate
> from the repo owner's own work in the history.

## Run it

```
go build -o restauth ./cmd/restauth
./restauth gencert                                  # cert.pem + key.pem (key is chmod 600)
./restauth hashpw alice >> users.txt                # type a password (8+ chars) on stdin
./restauth serve -redirect 127.0.0.1:8080           # https://127.0.0.1:4443, http :8080 -> https

curl --cacert cert.pem -u alice:PASSWORD -H 'Content-Type: application/json' \
     -d '{"text":"ship part 6","tags":["go"],"due":"2026-11-20T09:00:00Z"}' https://127.0.0.1:4443/task/
curl --cacert cert.pem -u alice:PASSWORD https://127.0.0.1:4443/task/
curl --cacert cert.pem https://127.0.0.1:4443/task/          # 401 + WWW-Authenticate: Basic realm="tasks"
curl -i http://127.0.0.1:8080/task/                          # 308 -> https://127.0.0.1:4443/task/
```

Test: `go test ./...` runs 19 tests in under a second. 5 for password hashing, 6 for the auth middleware,
2 for the store, 1 for certificate generation, and 5 for the server. The server tests run the real handler over
real TLS, with our own TLS policy and a freshly generated certificate.

## Layout

| Path | What it does |
| --- | --- |
| `internal/password` | `Hash`, `Verify`, `NeedsRehash`. Encoded as `pbkdf2-sha256$<iterations>$<salt>$<key>` |
| `internal/auth` | `LoadUsers` (a `name:hash` file), `Authenticate`, the `BasicAuth` and `RequireTLS` middleware, the user in the request context |
| `internal/taskstore` | The series' in-memory task store, with an owner on every task |
| `internal/tlsutil` | `SelfSigned` (ECDSA P-256 certificate) and `ServerConfig` (TLS 1.3 only) |
| `internal/server` | Routes (Go 1.22 `METHOD /path/{param}/` patterns), JSON handling, security headers, the HTTP to HTTPS redirect |
| `cmd/restauth` | `gencert`, `hashpw`, `serve` (with timeouts and graceful shutdown) |

Routes: `GET /healthz` (no auth), `GET /whoami`, `POST|GET|DELETE /task/`, `GET|DELETE /task/{id}/`,
`GET /tag/{tag}/`, `GET /due/{y}/{m}/{d}/`.

## What I learned

- **Basic auth is only as safe as the connection it runs over.** The `Authorization: Basic ...` header is just
  base64 of `user:password`. It isn't encrypted, and it is sent with *every* request. So TLS isn't an extra here,
  it's required. The handler enforces that itself: `RequireTLS` sits outside the auth middleware and returns 403
  for any request where `r.TLS == nil`, *before* any credentials are read. A test mounts the handler on a plain
  HTTP listener with an authenticator that panics if it is ever called.
- **The 401 challenge is part of the protocol.** Rejecting with a bare 401 isn't enough. The
  `WWW-Authenticate: Basic realm="tasks", charset="UTF-8"` header is what makes browsers show a login dialog and
  tells clients which scheme to use. `r.BasicAuth()` does the parsing, and a missing header, a different scheme
  (`Bearer`) or broken base64 all fail the same way.
- **Store passwords with a slow, salted hash plus its parameters.** A random 16-byte salt per password means two
  users with the same password get different hashes, and precomputed tables don't help an attacker. 600,000
  PBKDF2 iterations makes each guess cost about 0.1 s. Keeping the scheme and iteration count in the stored string
  lets the cost go up later. `NeedsRehash` spots old hashes so they can be re-hashed on the next successful login.
- **A stored hash is input, so it gets validated.** `Verify` refuses iteration counts above 10 million. Otherwise
  someone who could edit `users.txt` could make one login attempt use minutes of CPU. It also rejects
  unknown schemes and salts or keys that are too short. `LoadUsers` refuses to start on plaintext passwords or
  duplicate users, so a bad users file fails at startup instead of failing quietly at login.
- **Compare secrets in constant time.** The final check is `subtle.ConstantTimeCompare`. A normal `==` returns at
  the first byte that differs, so in principle the response time leaks how much of the key matched.
- **Unknown users have to cost the same as wrong passwords.** If an unknown username returned straight away, it
  would answer in microseconds, while a wrong password takes about 100 ms. That gap tells an attacker which
  usernames exist. `Authenticate` checks the password against a **dummy hash with the same cost** when the user
  doesn't exist, and only then says no. The dummy is built in `LoadUsers`. My first version built it lazily, and
  timing it with curl showed the problem: the *first* unknown-user login was slower (it paid for building the
  dummy hash), which is a small timing leak of its own. A test counts `verify` calls and checks the dummy's cost
  matches. With the dummy check removed, that test fails.
- **Authentication and authorization are different questions.** Basic auth answers "who is this?" The store
  answers "can they see this task?" Every store method takes the owner. Bob asking for Alice's task gets the
  **same 404 and the same body** as a task that doesn't exist, not a 403. A 403 would confirm that ID 0 exists.
  With the owner check removed from `Get`, both the store test and the server test fail.
- **Unknown routes are behind auth too.** Only `/healthz` is mounted outside `BasicAuth`. Everything else,
  including paths that don't exist, returns 401 to anonymous clients, so they can't map out the API first.
- **Self-signed certificates and trust.** `SelfSigned` makes an ECDSA P-256 certificate that is its own CA, with
  the host names and IPs as SANs. Modern clients ignore the CN. Go's default client refuses it (a test checks
  this), so the client has to be told to trust it: `curl --cacert cert.pem`, or a `x509.CertPool` in Go. Don't
  reach for `-k` / `InsecureSkipVerify`. That turns off the check that you're talking to the right server, and then
  TLS no longer protects the password from a man in the middle. The private key is written with mode `0600`.
- **The TLS policy can be one line.** `MinVersion: tls.VersionTLS13`. TLS 1.3 removed the weak cipher suites,
  RSA key exchange and renegotiation, and Go doesn't let you configure 1.3 cipher suites anyway. A test checks
  that 1.3 is negotiated and that a client capped at TLS 1.2 fails the handshake. The server log shows
  `client offered only unsupported versions`.
- **HSTS and the redirect work together.** The plain-HTTP listener only sends a 308 to the HTTPS URL (308 rather
  than 301 so a POST stays a POST). Every HTTPS response carries `Strict-Transport-Security`, so after one visit the
  browser goes straight to HTTPS and never sends credentials over plain HTTP again. The redirect rejects `Host`
  values containing `/`, `\` or `@`, which could otherwise change what the `Location` URL points at.
- **Small hardening that's easy to forget:** `Cache-Control: no-store` on per-user responses,
  `X-Content-Type-Options: nosniff`, `http.MaxBytesReader` (64 KiB, returns 413),
  `DisallowUnknownFields` (so a client can't send `"owner":"bob"`), a JSON content-type check (415), and server
  timeouts (`ReadHeaderTimeout` against slowloris clients).

## Things I'd do next

- Rate-limit failed logins per user and per IP. A slow hash makes guessing expensive, but it doesn't stop it.
  The token-bucket limiter from `2026-11-10-go-middleware` would fit here.
- Part 6 points out that Basic auth sends the password every time. The usual next step is logging in once and
  getting a short-lived token or session cookie back.
- Reload `users.txt` on SIGHUP, and re-hash on login when `NeedsRehash` says so.
- Use a real certificate (ACME / Let's Encrypt) instead of a self-signed one.
