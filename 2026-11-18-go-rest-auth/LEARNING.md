# Learning: HTTPS + Basic auth for a Go REST server (standard library only)

**Source:** "REST Servers in Go, Part 6 - authentication" (eli.thegreenplace.net), from the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial site was blocked from the build environment, so this follows the topic of Part 6, not its text:
serve the series' task API over TLS and protect the write routes with HTTP Basic auth. Everything uses the
Go 1.24 standard library, including password hashing (`crypto/pbkdf2` instead of the `x/crypto/bcrypt` module).

> Written by the daily-project routine (Claude). Commits are authored as `Claude`, so they are separate
> from the repo owner's own work in the history.

Run (from this folder):

```
go run . gencert                                   # writes cert.pem and key.pem (key is 0600)
echo "alice:$(echo wonderland | go run . hashpw)" > users.txt
go run . serve                                     # https on :8443, redirect from :8080

curl --cacert cert.pem https://localhost:8443/task/                       # public read
curl --cacert cert.pem -u alice:wonderland -H 'Content-Type: application/json' \
     -d '{"text":"buy milk","tags":["home"]}' https://localhost:8443/task/ # 201, owner "alice"
curl --cacert cert.pem -u alice:wonderland https://localhost:8443/whoami
curl -i http://localhost:8080/task/1                                      # 308 -> https
```

`cert.pem`, `key.pem` and `users.txt` are git-ignored. Generate your own; never commit a private key.

Test: `go vet ./... && go test -race -v ./...` runs 12 tests (about 7 s, most of it one timing test).
Five cover passwords and the users file, one the certificate, two the middleware and redirect without a
network, one user-enumeration timing, and three run the real API over a TLS listener with a generated cert.

## Layout

- `certs.go`: `GenerateSelfSigned` creates an ECDSA P-256 key and a self-signed cert with proper SANs.
- `passwords.go`: `HashPassword` / `VerifyPassword` (PBKDF2-HMAC-SHA256, salted, self-describing format),
  `UserStore.Authenticate`, and `ParseUsersFile`.
- `auth.go`: the `BasicAuth` middleware and `UserFromContext`.
- `server.go`: `TLSConfig` (TLS 1.3 only), `NewHTTPSServer` (with timeouts), `RedirectToHTTPS` and `HSTS`.
- `tasks.go`: the in-memory `TaskStore` and routes. `GET` is public. `POST /task/`, `DELETE /task/{id}` and
  `GET /whoami` need auth, and only a task's owner may delete it.
- `main.go`: the `gencert`, `hashpw` and `serve` subcommands.

## What I learned

- **Basic auth is only as safe as the transport.** The `Authorization` header is just `base64(user:pass)`, which
  anyone on the path can decode, and it is sent on *every* request. So TLS isn't optional, and the middleware
  refuses any request where `r.TLS == nil` with a 403 before it even looks at the credentials.
- **A 401 needs a `WWW-Authenticate` header.** `Basic realm="tasks", charset="UTF-8"` is what makes a browser
  show its login box and tells API clients which scheme to use. A bare 401 is technically wrong.
- **Hosts go in the SAN list, not the Common Name.** Go (and every browser) ignores CN for hostname checks.
  IPs must go in `IPAddresses`, not `DNSNames`. Putting `127.0.0.1` in the DNS list makes verification fail,
  and a test catches that.
- **Trusting a self-signed cert means adding it to the client's root pool**, not turning verification off.
  The test client builds an `x509.CertPool` from `cert.pem`. A plain `http.Get` fails with
  `x509.UnknownAuthorityError`, which a test checks. `InsecureSkipVerify` would "work" and also accept any attacker's cert.
- **TLS 1.3 only is the easy secure setting.** With `MinVersion: tls.VersionTLS13` there is no cipher list to tune.
  Go chooses the suites, and old protocol versions are refused during the handshake (a test with a TLS 1.2 client).
- **Store passwords as slow, salted hashes, and put the parameters in the string.**
  `pbkdf2-sha256$600000$<salt>$<key>` records the algorithm and iteration count, so the cost can be raised later
  without breaking old entries. A random 16-byte salt means two users with the same password get different
  hashes (tested). 600k iterations is the OWASP number for PBKDF2-SHA256; the tests use 1,000 to stay fast.
- **Compare in constant time, and make "no such user" cost the same as "wrong password".** `subtle.ConstantTimeCompare`
  hides how many bytes matched. Returning early for an unknown username would answer much faster and reveal which
  usernames exist. So unknown users are checked against a dummy hash at the same cost. A timing test fails if
  that early return is added.
- **Authentication isn't authorization.** BasicAuth only says *who* you are and puts the name in the request
  `context`. The store separately checks that the caller *owns* the task before deleting it: bob gets a 403 on
  alice's task, not a 401.
- **The HTTP port should only redirect, and with 308.** 308 keeps the method and body (301/302 let clients turn a
  POST into a GET). The redirect can't undo a password that was already sent in plaintext, which is why `HSTS`
  (`Strict-Transport-Security`) tells browsers to skip plain HTTP for this host from then on.
- **A zero-value `http.Server` has no timeouts.** `NewHTTPSServer` sets `ReadHeaderTimeout`, `ReadTimeout`,
  `WriteTimeout` and `IdleTimeout`, so a slow client can't hold a connection open forever (Slowloris).

## Known limits

- No rate limiting or lockout, so passwords can be guessed online (Part 5's token-bucket middleware would fit here).
- Basic auth has no logout or expiry. A browser keeps sending the credentials until it's closed. Sessions or
  tokens would fix that.
- The self-signed cert is for development. A real deployment would use a CA-issued cert (Let's Encrypt via
  `golang.org/x/crypto/acme/autocert`, for example).
- Tasks are kept in memory and lost on restart. The users file is read once at startup.

License: MIT, see the LICENSE file at the repository root. Topic credit: "REST Servers in Go" by Eli Bendersky,
https://eli.thegreenplace.net/2021/rest-servers-in-go-part-6-authentication/
