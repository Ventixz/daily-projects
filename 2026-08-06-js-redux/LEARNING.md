# Build Yourself a Redux (JavaScript)

**Source:** "Build Yourself a Redux" (JavaScript: Miscellaneous section), from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
There's no single canonical tutorial URL for this one in the list — it points at the general
practice of reimplementing Redux's core from scratch, which is also how Redux's own docs teach
the library (`Redux from Scratch`-style walkthroughs). I built against my own knowledge of Redux's
actual source, not a copy of it.

Picked and built end-to-end in one sitting, so this folder contains the finished implementation
directly at the project root (no separate `reference/`).

## What it is

A dependency-free clone of Redux's core, split the same way the real library is:

- `src/createStore.js` — the store itself: `getState`, `dispatch`, `subscribe`, `replaceReducer`.
  Owns a single `currentState` plus a copy-on-write listener list.
- `src/combineReducers.js` — turns `{ todos: todosReducer, filter: filterReducer }` into one
  reducer over `{ todos, filter }`, with the same shape-validation Redux does on the first call.
- `src/compose.js` / `src/applyMiddleware.js` — the store enhancer that lets `dispatch` be wrapped
  by a chain of middleware instead of calling the reducer directly.
- `src/middleware/thunk.js`, `src/middleware/logger.js` — two real middleware built on that chain:
  thunk (dispatch a function, get async actions) and a logger (observe state before/after).
- `examples/todo-demo.js` — a runnable script wiring all of the above together: `combineReducers`
  + `applyMiddleware(thunk, logger)` + a fake-async action creator.

## Run it

```bash
cd 2026-08-06-js-redux
node --test              # 23 tests, zero dependencies (uses node:test)

node examples/todo-demo.js
```

## What it actually teaches

- **A store's listener list has to be copy-on-write, or subscribing during a dispatch corrupts
  the dispatch in progress.** `createStore.js`'s `dispatch` snapshots `nextListeners` into a local
  `listeners` array *before* the notification loop starts, and `subscribe`/`unsubscribe` always
  mutate `nextListeners` (via `ensureCanMutateNextListeners`, which clones it the first time it's
  touched since the last dispatch), never `currentListeners` directly. Without that split, a
  listener that calls `store.subscribe()` on itself — a real pattern, e.g. a one-shot listener that
  re-subscribes conditionally — would splice a new function into the array `for...of` is actively
  iterating, and depending on engine internals either skip entries or call the brand-new listener
  in the same round it was added. The test
  `"a listener that subscribes during dispatch is not called until the next dispatch"` pins this:
  it asserts the newly-added listener fires zero times in the dispatch that created it, exactly
  one time in the next.
- **`combineReducers` can't throw when you call it — only when you use it.** `combineReducers()`
  returns a plain reducer function; nothing about constructing that function is allowed to throw,
  because callers treat "I have a reducer" as unconditionally true the moment the call returns.
  So `assertReducerShape` runs immediately but its result goes into a closed-over
  `shapeAssertionError` variable, and the *returned* `combination` function re-throws it on its
  first invocation. I initially wrote a test asserting `combineReducers({ broken })` throws
  directly — it doesn't, and shouldn't; the corrected test
  (`"a bad reducer shape is caught at combine time but only thrown on first use"`) asserts the
  construction succeeds and the first `combined(state, action)` call is what blows up. Real Redux
  does exactly this, and now I know why: it's the same reason a broken SQL query fails when you
  *run* it, not when you *prepare* it.
- **`replaceReducer` is a hot-swap, not a reset — because the new reducer's first call is not
  passed `undefined`.** My first version of the `"replaceReducer"` test assumed a freshly-swapped
  `(state = "unchanged") => state` reducer would show its default value. It doesn't: `dispatch`
  runs the *new* `currentReducer` with whatever `currentState` already held (`1`, from an earlier
  `INCREMENT`), and JS default parameters only apply when the argument is literally `undefined` —
  not for "the reducer just changed." That's not a bug, it's the entire point of the API: swapping
  a reducer (e.g. after a code-split chunk loads with more reducers, Redux's actual `replaceReducer`
  use case) is only useful if the state tree survives the swap so the new reducer can pick up where
  the old one left off.
- **Middleware order is call-order, and that's `compose`'s right-to-left reduce, not a queue.**
  `applyMiddleware`'s `chain = middlewares.map(mw => mw(api))` produces `[wrappedA, wrappedB]` in
  the order they were passed, but composing them left-to-right as functions (`a(b(x))`) makes `a`
  the *outer* layer — the first thing a caller's `dispatch(action)` hits, and the last thing that
  runs on the way back out. `compose(...chain)(store.dispatch)` relies on `Array.prototype.reduce`
  building exactly that nesting. The test `"applyMiddleware runs middleware in the order given,
  outermost first"` traces `a:before, b:before, b:after, a:after` — a stack, not a pipeline — which
  is what makes logger-wraps-thunk vs. thunk-wraps-logger a real, order-sensitive choice rather than
  cosmetic.
- **The placeholder `dispatch` during middleware construction isn't defensive paranoia — a
  middleware really can call it eagerly.** `applyMiddleware` builds `middlewareAPI.dispatch` as a
  closure over a `let dispatch` that starts out as a function that only throws, precisely because
  `middlewares.map(mw => mw(middlewareAPI))` calls every middleware's outer function *before*
  `dispatch` is reassigned to the real composed chain. A middleware that reads `api.dispatch` and
  invokes it synchronously (not inside its returned `next => action =>` layer, but eagerly, during
  its own setup) would otherwise silently call `undefined` or an incomplete chain. The test
  `"dispatching before the middleware chain finishes constructing throws"` calls `api.dispatch`
  from inside the middleware factory itself to prove the guard is reachable, not just theoretical.

## What I'd add next (stretch goals I skipped for scope)

- **`bindActionCreators`.** The other piece of Redux's public API I skipped — wraps an object of
  action creators so callers can invoke `actions.addTodo(...)` instead of
  `dispatch(addTodo(...))`. Pure convenience, no new mechanism, which is why it lost to the
  middleware chain for time.
- **DevTools-style time travel.** `createStore` only ever holds one `currentState`; a real
  time-travel debugger keeps a history of every state the store has passed through, plus the
  ability to re-dispatch from any point. Would need the store to record instead of overwrite.
- **A `combineReducers` that nests.** Mine handles one flat level; Redux apps in practice nest
  `combineReducers` calls arbitrarily deep for feature-sliced state, which this doesn't test.
- **Immutable-update helpers.** Every reducer in the demo hand-writes `[...state, x]` /
  `{...state, done: true}`; a real app usually reaches for something like Immer once nesting gets
  deep enough that hand-written spreads become error-prone.
