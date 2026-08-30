# Build a Web Framework in Less Than 20 Lines of Code (TypeScript)

**Source:** ["How to Build a Web Framework in Less Than 20 Lines of
Code"](https://www.pubnub.com/blog/build-yourself-a-web-framework-in-less-than-20-lines-of-code/),
from the JavaScript section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
`pubnub.com` is outside this environment's network allowlist (same
restriction earlier days hit with `medium.com`, `freecodecamp.org`, and
`rcoh.svbtle.com`), so this is the idea the title names, built from
scratch and in TypeScript instead of plain JS: routing, middleware, and
`next()`-based continuation, layered directly on Node's raw `http`
module — no Express, no dependencies beyond the TypeScript compiler.

## What to build

- `compilePath(path)` — turns `"/users/:id"` into a `RegExp` plus the
  ordered param names its capture groups correspond to.
- `App` — a single ordered stack of "layers" (`app.use()` for
  middleware, `app.get()`/`.post()`/`.put()`/`.patch()`/`.delete()` for
  routes), dispatched by walking the stack and calling `next()` to
  advance — the same model Express itself uses internally.
- Request/response augmentation: `req.params`, `req.query`, `req.body`
  (JSON auto-parsed when `Content-Type` says so), `res.status()`,
  `res.json()`, `res.send()`.
- Error-handling middleware: a handler with four parameters
  (`(err, req, res, next)` instead of three) is picked out by function
  arity and only runs once something calls `next(err)` or a handler
  throws.

## What it teaches

- A "framework" here is really one thing: an ordered list of functions
  plus a `next()` that decides which one runs next. Routing (`app.get`)
  and middleware (`app.use`) aren't different mechanisms — they're the
  same stack entry with a different matcher (exact-path-and-method vs.
  prefix-and-any-method).
- Distinguishing regular handlers from error handlers by counting
  declared parameters (`fn.length === 4`) is a real technique, not a toy
  one — it's exactly how Express does it.
- A handler that *throws* and a handler that calls `next(err)` need to
  produce the same outcome, but nothing gives you that for free — the
  dispatcher has to explicitly catch around every handler invocation and
  fold a throw into a `next(thrown)` call, or thrown errors silently skip
  the entire error-handling chain.

## Setup

- Plain Node.js + TypeScript, no frameworks. `npm install` pulls in only
  `typescript` and `@types/node` as dev dependencies.

## Milestones

1. `compilePath()` and its own test file — static segments, `:param`
   capture, a wildcard `*`, and regex-escaping of literal characters.
2. `App` with `use()`/`get()`/`post()`/etc. building a single layer
   stack, and a `dispatch()` that walks it via `next()`.
3. Request/response augmentation (`req.params`/`query`/`body`,
   `res.status()`/`json()`/`send()`), including a JSON body parser that
   turns a malformed body into a 400 through the same error-handling
   path as anything else.
4. Error-handling middleware, selected by arity, that only runs when an
   error is in flight.
5. A demo app (`src/main.ts`) exercising all of it: global logging
   middleware, a path-scoped auth check on `/admin/*`, a param route, a
   JSON-echoing POST route, and a route that deliberately throws.
6. An integration test suite that spins the real app up on an ephemeral
   port and drives it with `fetch` — not unit tests of the dispatcher in
   isolation, but the same HTTP surface a client would actually see.
