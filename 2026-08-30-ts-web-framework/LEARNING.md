# Build a Web Framework in Less Than 20 Lines of Code (TypeScript)

**Source:** ["How to Build a Web Framework in Less Than 20 Lines of
Code"](https://www.pubnub.com/blog/build-yourself-a-web-framework-in-less-than-20-lines-of-code/),
from the JavaScript section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
`pubnub.com` isn't reachable from this environment (same allowlist
restriction earlier days hit with `medium.com`, `freecodecamp.org`, and
`rcoh.svbtle.com`), so what's here isn't a port of that article's actual
code — it's the idea its title names, built from scratch: routing,
middleware, and `next()`, in TypeScript, directly on Node's `http`
module.

## What it is

- `src/path.ts` — `compilePath(path)`: turns `"/users/:id/posts/:postId"`
  into a `RegExp` with one capture group per `:name` segment, plus the
  ordered list of names those groups correspond to. A `*` segment
  captures the rest of the path under the key `"wildcard"`. Literal
  segments are regex-escaped so a path like `/a.b` matches a literal
  dot, not "any character."
- `src/app.ts` — the framework itself:
  - `App` holds a single ordered array of `Layer`s. `app.use(fn)` and
    `app.use(prefix, fn)` push a layer that matches by path prefix and
    any method; `app.get(path, fn)` (and `post`/`put`/`patch`/`delete`)
    push a layer that matches by exact compiled path *and* method.
  - `dispatch(req, res, index, err)` walks the stack from `index`,
    decides whether the current layer applies, and calls the handler
    with a `next` that recurses to `index + 1`. Whether `err` is defined
    decides which *kind* of layer can run next: normal layers are
    skipped while an error is in flight, and error-handling layers (four
    declared parameters, checked via `fn.length === 4`, the same trick
    Express uses) are skipped when it isn't.
  - `augmentResponse()` bolts `status()`, `json()`, and `send()` onto the
    raw `http.ServerResponse` for the duration of one request.
  - The body parser reads the full request into a `Buffer`, and if
    `Content-Type` says JSON, parses it — a parse failure becomes an
    `HttpError(400, ...)` routed through the same `dispatch()` path as
    every other error, not a special case.
- `src/main.ts` — a demo app: global logging middleware, a path-scoped
  fake-auth check on `/admin/*`, a `/users/:id` param route, a
  JSON-echoing `POST /echo`, a `/boom` route that throws on purpose, and
  one error-handling middleware at the end that turns whatever reached it
  into a JSON error response.
- `src/path.test.ts` — 7 checks on `compilePath()` in isolation: static
  matching, single and multiple params, param segments not crossing a
  `/`, the wildcard, a trailing slash, and regex-escaping.
- `src/app.test.ts` — 9 integration checks that spin the real `App` up
  with `app.listen(0)` (OS-assigned port) and drive it with `fetch`,
  covering routing, params, query strings, JSON body round-tripping, a
  malformed-JSON 400, scoped middleware (both admitted and blocked, plus
  a check that a route merely *starting with* the same string as the
  prefix doesn't match), 404s, a thrown handler, and that global
  middleware really does run on every request including ones that 404.

## Run it

```bash
cd 2026-08-30-ts-web-framework
make test   # 16 checks: 7 for compilePath(), 9 integration tests over real HTTP
make run    # starts the demo app on :3000
```

## What it actually teaches

- **Routing and middleware are the same mechanism wearing two hats.**
  Once `dispatch()` exists as "walk a list, run whichever entries match,
  advance on `next()`," `app.get(path, fn)` and `app.use(prefix, fn)`
  are just two different *matchers* pushed onto the same stack — an
  exact-path-and-method test versus a prefix-and-any-method test. There
  was no separate "router" data structure needed once that clicked;
  splitting per-route matching (`compilePath`, `src/path.ts`) out into
  its own tested unit was still worth doing, but composing it into a
  full `Router` class *before* the middleware-aware stack existed would
  have been the wrong abstraction — you can't express "this middleware
  runs before all routes under `/admin`" in a structure that only knows
  about individual routes.
- **Picking an error handler out by counting its declared parameters is
  not a toy trick — it's load-bearing.** `isErrorHandler()` is
  `fn.length === 4`. That means a normal three-argument handler passed
  where the code expects `(err, req, res, next)` would be silently
  skipped rather than throwing a type error, and `app.test.ts`'s
  "handles a thrown error" case is really testing that this arity
  dispatch and the try/catch below cooperate correctly, not just that
  *some* 500 comes back.
- **A thrown exception and a `next(err)` call are not the same thing
  unless the dispatcher makes them the same thing — and the first
  version of this code didn't.** The `/boom` route does
  `throw new Error("thrown from a handler, on purpose")` with no
  try/catch of its own. The very first `dispatch()` had no try/catch
  around the handler invocation either, on the theory that `handle()`
  being an `async` function would catch a synchronous throw anywhere in
  its call chain and turn it into a rejected promise — which is true,
  but that rejection surfaced in `listen()`'s outer
  `this.handle(req, res).catch(...)`, a plain safety net that never runs
  through the app's own error-handling layer at all. `app.test.ts`'s
  first run of "a handler that throws synchronously is caught and routed
  to error middleware" failed on exactly this: the response body was
  `{"error":"Error: thrown from a handler, on purpose"}` (note the
  `"Error: "` prefix) instead of the expected
  `{"error":"thrown from a handler, on purpose"}`, because the outer
  catch's `String(err)` — not the framework's own
  `err instanceof Error ? err.message : String(err)` in `main.ts`'s
  error middleware — was the code that actually produced the response.
  Two completely different pieces of error-formatting code, and the test
  caught that the wrong one was firing. The fix was wrapping both handler
  invocations inside `dispatch()` in `try { ... } catch (thrown) {
  next(thrown) }`, so a throw becomes exactly the same `next(err)` path a
  handler could have taken deliberately, and the app's own
  error-handling middleware — not a framework-level fallback — is what
  actually produces the response.
- **"Does this middleware apply to this path" needs a boundary check,
  not `startsWith`.** `prefixTest("/admin")` matching `/administrator`
  would be a real bug — a path that happens to share a string prefix
  with a protected route but isn't actually under it. `prefixTest()`
  requires the matched path to equal the prefix or continue with `/`,
  and `app.test.ts` asserts `/administrator` 404s (falls through
  everything) rather than triggering the `/admin` auth middleware.

## Deliberate scope cuts

- **No async handler support.** A handler declared `async (req, res) =>
  { await x(); throw ... }` produces an unhandled promise rejection, not
  a routed error — `dispatch()` calls handlers synchronously and only
  catches a *synchronous* throw. This is the same limitation Express 4
  itself has (fixed only in Express 5, or by wrapping every async
  handler by hand); this project doesn't attempt it.
- **No route-level middleware, only global (`use(fn)`) and prefix-scoped
  (`use(prefix, fn)`).** Express-style `app.get(path, mw1, mw2, handler)`
  chains are supported (multiple handlers per verb call), but there's no
  way to attach middleware to one specific route without also making it
  a full extra layer some other route might accidentally match.
- **No route param constraints or optional segments** (`:id(\\d+)`,
  `:id?`) — a param segment is always `[^/]+`, required, exactly one
  path component.
- **In-place text patching-equivalent for responses doesn't apply here,
  but the analogous cut is: no streaming responses.** `res.json`/`send`
  always call `res.end()` immediately; there's no support for a handler
  that wants to stream chunks and finish later.

## What I'd add next

- **A proper async-handler wrapper**, so `app.get(path, asyncHandler(fn))`
  (or just detecting a returned Promise and attaching `.catch(next)` to
  it inside `dispatch()`) closes the gap noted above without requiring
  every handler author to remember to catch their own async errors.
- **Route-level middleware arrays**, so a single `app.get()` call could
  take `[requireAuth, validateBody]` ahead of the real handler without
  those checks becoming globally-reachable layers.
- **A tiny router-mounting feature** (`app.use('/api', subApp)`), to see
  whether the single flat layer stack still holds up once routes are
  meant to be composed from smaller pieces instead of all registered on
  one `App`.
