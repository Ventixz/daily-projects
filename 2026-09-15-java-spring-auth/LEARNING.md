# Spring Boot App with User Authentication

**Source:** ["Securing a Web Application"](https://spring.io/guides/gs/securing-web/), the Java /
Spring Boot entry in
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Built as a stateless REST API rather than the guide's session-backed MVC form login: registration
and login issue a signed JWT, and every other endpoint is protected by validating that token on
each request. Backed by Spring Security 6, Spring Data JPA, an in-memory H2 database, and
`jjwt` for token signing/parsing.

## What it is

- `POST /api/auth/register`, `POST /api/auth/login` — public, return a JWT + username + role.
- `GET /api/me` — any authenticated user; echoes back who the token says you are.
- `GET /api/admin/ping` — requires `ROLE_ADMIN`.
- `JwtAuthFilter` — a `OncePerRequestFilter` that reads `Authorization: Bearer <token>`,
  validates it via `JwtService`, and populates `SecurityContextHolder` before Spring Security's
  own authorization checks run.
- `ApiExceptionHandler` — maps duplicate-username, bad-credentials, and validation failures to
  409/401/400 with a small JSON body instead of Spring Boot's default whitelabel error payload.

## Run it

```bash
cd 2026-09-15-java-spring-auth
mvn test                              # 14 tests: MockMvc integration tests + JwtService unit tests
mvn spring-boot:run                   # starts on :8080

curl -X POST localhost:8080/api/auth/register \
  -H 'Content-Type: application/json' \
  -d '{"username":"alice","password":"correct-horse"}'
# => {"token":"...","username":"alice","role":"USER"}

curl localhost:8080/api/me -H "Authorization: Bearer <token>"
curl localhost:8080/api/admin/ping -H "Authorization: Bearer <token>"   # 403 for a USER token
```

## What it actually teaches

- **A stateless API needs an explicit `AuthenticationEntryPoint`, or missing credentials come
  back as 403 instead of 401.** Out of the box, an unauthenticated request that fails an
  `authenticated()` check gets Spring Security's default `AccessDeniedHandler` — 403 — because
  the request already carries an `AnonymousAuthenticationToken` by the time the authorization
  check runs, and `ExceptionTranslationFilter` only routes to the 401-producing entry point when
  it sees that specific anonymous/null-authentication case *at translation time*. The fix is a
  one-line `.exceptionHandling(ex -> ex.authenticationEntryPoint(...))` that sends a real 401 for
  an `AuthenticationException`, leaving `AccessDeniedException` (authenticated, wrong role) to
  still 403. Easy to miss because both cases print as "it's not letting me in" during manual
  testing unless you actually check the status code.

- **The best bug here never showed up in the test suite at all — only in a real `curl` against
  the packaged jar.** `GET /api/admin/ping` with a valid `ROLE_USER` token was coming back **401**
  instead of the expected 403. Turning on `logging.level.org.springframework.security=TRACE`
  showed why: the first pass through the filter chain correctly produces
  `AccessDeniedHandlerImpl: Responding with 403 status code` — but `response.sendError(403)`
  triggers Tomcat's container-level error-page mechanism, which **forwards the same request to
  `GET /error`** and re-runs it through the *entire* Spring Security filter chain a second time.
  On that second pass, `JwtAuthFilter` (a `OncePerRequestFilter`) sees its own
  "already filtered" attribute still set on the request object — forwards reuse the same
  `HttpServletRequest` — and silently no-ops instead of re-authenticating. `/error` isn't
  `permitAll()`, so it gets evaluated as anonymous, fails `authenticated()`, and *that* denial —
  now genuinely anonymous — correctly routes to the 401 entry point. The 401 from the `/error`
  dispatch is what actually reaches the client, silently overwriting the correct 403 that had
  already been logged. The fix is `.requestMatchers("/error").permitAll()`. The bigger lesson:
  **`MockMvc` never exercises this path** — it doesn't run a real servlet container's error-page
  forwarding, so all 14 automated tests passed throughout, including the one asserting 403 on
  this exact endpoint. Only booting the actual jar and hitting it with `curl` caught it.

- **`jjwt`'s parser enforces the `exp` claim itself; there's no separate "parse, then check
  expiry" step to write.** The first draft of `JwtService.isTokenValid` parsed the token, then
  called a separate `isExpired()` that re-extracted the `exp` claim and compared it to `now`. That
  method could never actually return `true` in practice: `Jwts.parser()...parseSignedClaims(token)`
  already throws `ExpiredJwtException` *during parsing* if the token is expired, so by the time
  code reaches a claims object to inspect, expiry has already been validated. Removed the dead
  check and rely on catching `ExpiredJwtException`, which the `expiredTokenIsInvalid` test
  (issues a token with a 1ms lifetime, sleeps 25ms, then validates) confirms actually exercises.

- **`@JsonIgnore` on the password hash is easy to add and easy to forget to verify.** It's a
  one-line annotation on `User.passwordHash`, but nothing forces you to prove it worked — a test
  asserting `jsonPath("$.passwordHash").doesNotExist()` on the register response is what actually
  closes the loop, since a typo'd getter name or a DTO that accidentally re-exposes the entity
  would otherwise pass silently.

## Known limitations (by design, to keep scope to ~2-4 hours)

- No refresh tokens or logout/revocation list — a JWT is valid for its full lifetime once issued,
  which is fine for a learning project but not for production.
- No rate limiting on `/api/auth/login`, so it's brute-forceable as written.
- `PasswordEncoder` is a plain `BCryptPasswordEncoder()` at the default cost factor; a real
  deployment would tune that against measured login latency.
- Registration always creates a `ROLE_USER`; promoting to `ROLE_ADMIN` has no endpoint — it's
  done by inserting the row directly (see `AuthIntegrationTests.adminUserCanReachAdminEndpoint`).
- Spring Security logs a `WARN` about the explicit `DaoAuthenticationProvider` bean shadowing
  autoconfiguration from the `UserDetailsService`/`PasswordEncoder` beans. Left as-is since it's
  functionally correct and is the pattern most JWT-with-Spring-Security guides use, but a
  production version would likely drop the manual bean and let Spring Security build the provider
  itself.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Credit: ["Securing a Web Application"](https://spring.io/guides/gs/securing-web/), Spring.io.
