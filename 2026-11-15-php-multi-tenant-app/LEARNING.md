# Learning: a multi-tenant app in PHP (database per tenant, roles, invitations, tenant-aware login)

**Source:** "Build a full-featured multi-tenant app with Laravel" by Ashok Gelal, Parts 0–4 (Introduction, Setup,
Roles and Permissions, Invitation, Tenancy-aware Authentication). From the PHP section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The series builds on Laravel, `hyn/multi-tenant` (one database per tenant, picked by subdomain) and
`spatie/laravel-permission`. Medium was blocked from the build environment, so this follows the series' outline,
not its text. It builds the same ideas **without a framework or Composer**: plain PHP 8.3, PDO and SQLite. That
way the parts the packages normally hide (picking the tenant, switching connections, migrating every tenant,
checking permissions) are all code you can read. Parts 5–7 (testing, user profile, deployment) aren't covered as
separate steps, but the test suite stands in for Part 5.

> Written by the daily-project routine (Claude). Commits are authored as `Claude`, so they are separate
> from the repo owner's own work in the history.

Run: `php -S 127.0.0.1:8080 public/index.php`, then send JSON to `http://localtest.me:8080` (sign-up) and
`http://<tenant>.localtest.me:8080`. `localtest.me` and all its subdomains resolve to 127.0.0.1 in public DNS,
so you don't need to edit `/etc/hosts`.
CLI: `php bin/tenant.php create acme "Acme Corp" "Ada" ada@acme.test acme-pass-1`, plus `list`, `migrate`, `delete`.
Test: `php tests/run.php`. 14 tests and 126 checks, including one that starts a real `php -S` server and sends
`Host` headers and cookies. No Composer, no PHPUnit. Needs the `pdo_sqlite` extension.

## Try it

```sh
php -S 127.0.0.1:8080 public/index.php &
J='Content-Type: application/json'
curl -s -H "$J" localtest.me:8080/signup \
  -d '{"subdomain":"acme","organization":"Acme","name":"Ada","email":"ada@acme.test","password":"acme-pass-1"}'
curl -s -c jar -H "$J" acme.localtest.me:8080/login -d '{"email":"ada@acme.test","password":"acme-pass-1"}'
curl -s -b jar -H "$J" acme.localtest.me:8080/invitations -d '{"email":"bob@acme.test","role":"member"}'
#   -> {"accept_path": "/invitations/<64 hex chars>"}  (a real app would email this link)
curl -s -H "$J" acme.localtest.me:8080/invitations/<token>/accept -d '{"name":"Bob","password":"bob-pass-12"}'
```

Routes: `POST /signup` (bare domain only). On a tenant host: `POST /login`, `POST /logout`, `GET /me`,
`GET|POST /projects`, `DELETE /projects/{id}`, `GET|POST /invitations`, `GET /invitations/{token}`,
`POST /invitations/{token}/accept`, `PUT /users/{id}/role`.

## Layout

- `src/TenantManager.php`: the shared "system" database (`subdomain → database file`). Handles sign-up (create
  the file, migrate it, seed roles, create the admin), picks the tenant from the `Host` header, and runs
  `migrateAll` and `delete`.
- `src/Migrator.php`: named migrations that run once each and are recorded in a `migrations` table. There are two
  sets: one for the system database, one for every tenant database.
- `src/Rbac.php`: roles → permissions → `can()` / `authorize()`, the last-admin guard, `setRole`.
- `src/Invitations.php`: hashed, expiring, single-use invitation tokens.
- `src/Auth.php`: per-tenant users, `password_hash`/`password_verify`, rehash on login.
- `src/Projects.php`: a small resource guarded by permissions.
- `src/App.php`: the HTTP routes as a pure function `(method, host, path, body, &session) → (status, json)`.
  `public/index.php` connects it to `$_SERVER` and `$_SESSION`.
- `tests/run.php`: the test runner (`check()` and `throws()` helpers).

## What I learned

- **"Multi-tenant" is really a choice of isolation level.** You can isolate tenants with a `tenant_id` column on
  every row (cheap, but one missing `WHERE` leaks data), with a schema per tenant, or with a database per tenant.
  The series uses a database per tenant, and so does this. The upside is that a query has no way to reach
  another tenant's rows, because they're in a different file. Both admins are user id 1 in their own tenant, and
  that's fine. The cost is that every schema change has to run N times (`migrateAll`), and anything that works
  across tenants (sign-up, billing, "which tenant is this host?") needs a separate system database.
- **Picking the tenant is a security boundary, not routing.** `resolveHost` strips the port, lower-cases the host,
  requires the exact `.<base domain>` suffix, and rejects nested labels. Without the suffix check,
  `acme.example.test.evil.com` would resolve to acme. An unknown subdomain gets a 404. It must not fall through to
  the sign-up site, or `typo.example.test` would show the marketing page instead of an error.
- **Never build a file path from the tenant's name.** The database file is `bin2hex(random_bytes(8)).sqlite` and
  the system database maps the subdomain to it. With that, a subdomain can't do path traversal, renaming a tenant
  doesn't mean moving a file, and a directory listing doesn't reveal customer names. The subdomain rules (a DNS
  label, 3–30 characters, no `--`, reserved names like `www` and `api`) are checked before anything touches disk.
- **Sessions have to remember which tenant they belong to.** PHP keeps all sessions in one place on the server.
  Browsers scope cookies by host, but anyone can copy a cookie into a request to another host. If the session held
  only `user_id = 1`, acme's admin would be signed in as globex's admin. The session stores `tenant_id` too, and a
  mismatch counts as logged out. Removing that check breaks three checks in two tests, including the real-HTTP one.
  The cookie is also host-only (no `Domain=`), `HttpOnly` and `SameSite=Lax`. The session id is replaced on login
  to stop session fixation.
- **Check permissions, not roles.** The code asks `can($user, 'delete projects')`, never "is this user an admin?".
  So a new role (`viewer`) took one entry in `Rbac::DEFAULTS` and no changes to the checks. The permission set is
  cached for each request, so the cache must be cleared whenever roles change. A test promotes a member and checks
  that they can invite right away.
- **The last admin needs protecting.** A tenant with no admin can never invite anyone or change a role again.
  `removeRole` refuses to remove the last admin. `setRole` runs inside a transaction, so a refused change rolls
  back completely, and the user doesn't end up with no role at all.
- **Treat an invitation token like a password.** It's 256 random bits. Only `sha256(token)` is stored, so a copy of
  the database can't be used to accept pending invitations. SHA-256 is enough here (no bcrypt needed) because the
  token is random, not something a person picked that could be guessed from a wordlist. Tokens expire after 72
  hours, are single-use, and a new invitation to the same address cancels the old link. Accepting marks the row
  with `UPDATE … WHERE accepted_at IS NULL` and checks `rowCount() === 1` *before* creating the user. That way two
  requests racing on the same link can't both succeed. If the password is too weak, the transaction rolls back and
  the link still works.
- **Signing up is a saga across two databases.** The tenant database is created, migrated and seeded first, and
  only then is the tenant added to the system database. If any step fails, the half-built file is deleted. The
  connection has to be closed before `unlink()`, because Windows won't delete an open SQLite file. A test makes
  sign-up fail after migration (blank admin name) and checks that no file is left behind.
- **A migration with several statements needs its own transaction.** `PDO::exec` runs `CREATE TABLE …; NOT SQL`
  statement by statement. Without the transaction, the table would stay behind with no `migrations` row, and the
  next run would fail on "table already exists".
- **Requiring JSON is a cheap CSRF guard.** A cross-site HTML form can only send form-encoded, multipart or plain
  text bodies. A cross-site `fetch()` with `Content-Type: application/json` triggers a CORS preflight, and this app
  never approves one. So state-changing requests with any other body type get a 415.
- **SQLite needs `PRAGMA foreign_keys = ON` on every connection.** Without it, `REFERENCES … ON DELETE CASCADE` is
  parsed and then silently ignored.
- **Logging in shouldn't reveal which emails exist.** When the email is unknown, `attempt()` still runs
  `password_verify` against a dummy hash. That makes "no such user" and "wrong password" take about the same time.
  It also calls `password_needs_rehash`, so stored hashes upgrade when PHP's default algorithm changes.

## Testing notes

To check the tests aren't trivially passing, I broke the code on purpose: removed the session tenant check,
the last-admin guard, the expiry/single-use condition, and the JSON content-type check. Each change made at least
one test fail. Removing the nested-subdomain check in `resolveHost` did **not** fail a test. Subdomain validation
already blocks dots, so no tenant called `x.acme` can exist, and the check is a second layer of protection rather
than the only one.

## Known limits

- A JSON API only, with no HTML views. The invitation link is returned in the response instead of being emailed.
- No login rate limiting, no password reset, no email verification, no custom domains (subdomains only).
- `migrateAll` runs tenants one after another and stops at the first failure. A real fleet needs per-tenant status
  and retries.
- SQLite file per tenant is right for a demo. In production you'd usually use a database or schema per tenant on
  Postgres/MySQL, and creating those needs privileges the app user shouldn't normally have.
