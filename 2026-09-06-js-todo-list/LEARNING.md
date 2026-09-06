# Build a Todo List App in JavaScript (JavaScript)

**Source:** ["Build a Todo List App in JavaScript"](https://github.com/dwyl/javascript-todo-list-tutorial),
from the JavaScript section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial's brief is add/toggle/remove against an in-memory array with
direct DOM writes. I kept the "no framework, no build step" constraint --
plain `<script type="module">`, nothing transpiled, nothing bundled -- but
swapped the parts that make a todo list actually interesting to build twice:
`localStorage` became IndexedDB with a real versioned schema migration,
"push to the end of an array" became fractional-index drag-reorder that
never needs to renumber a neighbor, and there's a command-based undo/redo
stack instead of no undo at all. Direct-render-every-time is deliberate, not
an oversight -- see `src/ui/render.js`'s header comment for why this project
is the wrong place to reach for `2026-08-29-js-virtual-dom`'s diff/patch
layer instead.

## What it is

- `src/core/order.js` -- fractional indexing: order keys are decimal-digit
  strings ("5", "52", "05", ...) read as a fraction with implied trailing
  zeros, so plain string comparison already sorts them correctly.
  `keyBetween(lo, hi)` returns a key strictly between two neighbors (or
  past one end, if the other bound is `null`) using `BigInt` midpoint
  arithmetic that grows the string by a digit whenever it runs out of room,
  rather than a float that eventually can't tell two neighbors apart.
- `src/core/todos.js` -- pure list operations (`addTodo`, `removeTodo`,
  `toggleTodo`, `editTodo`, `setOrder`, `sortByOrder`, `filterTodos`,
  `orderForPosition`). No DOM, no storage, no `Date.now()` calls buried
  inside -- everything a caller might want to control is a parameter.
- `src/core/history.js` -- a `History` class plus command factories
  (`addCommand`, `removeCommand`, `toggleCommand`, `editCommand`,
  `reorderCommand`). Each command captures only what it needs to invert
  itself (the removed record, the previous text, the previous order key),
  not a full-state snapshot.
- `src/storage/migrations.js` -- the pure half of the IndexedDB schema
  upgrade: `migrateV1ToV2(records)` backfills the `order` field a v1
  record never had, from its `createdAt`.
- `src/storage/db.js` -- the impure half: `indexedDB.open` with
  `onupgradeneeded`, and promisified `getAll`/`put`/`remove` wrapping the
  request-callback API.
- `src/ui/render.js` -- rebuilds the `<ul>` from scratch on every render;
  roving-tabindex keyboard navigation (arrow keys move focus between
  items, not app state); native HTML5 drag-and-drop for reordering.
- `app.js` -- wires it together: every mutation goes through `mutate()`,
  which runs the command, re-renders, and diffs before/after state to
  decide what to write to IndexedDB (one code path handles do, undo, and
  redo identically, see below).
- `server.js` -- a static file server built on `node:http`/`node:fs` only,
  no framework, used for `make serve` and as the origin the e2e tests point
  a real browser at.
- `tests/unit/` -- 33 hand-rolled assertions (no test framework) against
  the pure core: order-key arithmetic, list operations, undo/redo
  sequencing, migration.
- `tests/e2e/` -- 8 Playwright-driven browser tests (add, toggle+reload,
  remove+reload, edit+reload, drag-reorder+reload, undo/redo, filters, and
  the live v1-to-v2 migration) against the actual app in actual Chromium.

## Run it

```bash
cd 2026-09-06-js-todo-list
make unit                 # 33 assertions, plain node, no deps
make e2e                  # 8 browser tests via the globally-installed playwright
make test                 # both
make serve                # http://localhost:8080
```

`make e2e` needs a `playwright` install reachable from `NODE_PATH` (this
project has zero npm dependencies of its own -- see `tests/e2e/harness.js`
for why `require`, not `import`, is what makes that borrowing possible).

## What it actually teaches

- **A float midpoint runs out of precision; a digit string doesn't, and
  proving that needs a test that actually exhausts the float version.**
  `order.js`'s `keyBetween` represents order keys as decimal-digit strings
  instead of the more obvious `(lo + hi) / 2` float approach, because
  repeatedly inserting between the same two neighbors (drag an item into
  the same gap over and over) eventually lands two floats on adjacent
  representable values, and the midpoint just returns one of them back --
  silently breaking the "always strictly between" invariant the whole
  feature depends on. `order.test.js`'s
  `repeatedly inserting at the same point never runs out of precision`
  bisects the same `["1", "2"]` gap 500 times in a row and asserts every
  single result is still strictly between its neighbors with no duplicates
  -- a test written specifically because I know where the float version
  would have started failing, not just a generic fuzz test.
- **Rendering happens synchronously; persisting doesn't -- and a test
  that reloads the page has to know the difference.** `app.js`'s `mutate()`
  calls `render()` immediately but `await`s the actual IndexedDB write
  separately, so a checkbox click looks done on screen before the write
  finishes. My first pass at the e2e suite clicked a checkbox, waited for
  `.todo-item.done` to appear in the DOM, and reloaded -- and lost the
  toggle on about half of runs, because the DOM update and the database
  write are two different amounts of time and only one of them has a DOM
  event to wait on. The fix wasn't a longer sleep; it's `pendingSync` (a
  module-level variable holding the in-flight write's promise) plus
  `window.__waitForSync()`, so a test can await the actual write instead of
  guessing how long it takes. Every `*-and-survives-a-reload` test in
  `app.spec.js` calls it before `page.reload()`.
- **Undo, redo, and "do it the first time" all being the same code path is
  what makes persistence correctness fall out for free instead of needing
  its own three-way test matrix.** `history.js`'s commands don't know
  IndexedDB exists; `app.js`'s `syncDb(before, after)` diffs the todos
  array from before a mutation to after it and writes exactly the
  difference, regardless of whether that mutation came from `run()`,
  `undo()`, or `redo()`. That's why the undo test in `app.spec.js`
  doesn't need a persistence variant of itself -- the same `mutate`-style
  wrapper handles the database side of undo automatically, because from
  `syncDb`'s point of view an undo is just another before/after pair.
- **A command needs exactly the state to invert itself, and getting that
  wrong either loses data or silently changes behavior.**
  `removeCommand(todos, id)` captures the *entire* removed record --
  including its `order` key -- at construction time, specifically so
  `undo` can put it back in its original list position via `addTodo`
  (which appends) plus the list already being re-sorted by `order` on
  render, rather than reappearing at the end of the visible list. The
  first version I wrote captured only `{id, text}` to save a field, which
  passed every test that didn't check *position* after undo -- it took
  writing `removeCommand: undo restores the exact removed record, order
  included` in `history.test.js`, asserting the full record via
  `assertEqual`, to notice the gap.
- **The drop target's exact pixel matters when "before" and "after" are
  decided by a strict `<` against the midpoint of a bounding box.**
  `render.js`'s drop handler compares `event.clientY` against
  `rect.top + rect.height / 2` to decide whether a dragged item lands
  before or after its target. Playwright's default `dragAndDrop` drops at
  the target's exact center, which lands precisely on that boundary --
  and depending on floating-point rounding of the two sides, the "obvious"
  drag-to-top test intermittently landed the dragged item *after* the
  target instead of before it. `app.spec.js`'s drag test passes an explicit
  `targetPosition: { x: 10, y: 2 }` (near the target's top edge) specifically
  to stay off that boundary, rather than trusting the default center-drop
  to be unambiguous.
- **A schema migration needs a test that starts from the old schema, not
  the new one with an extra assertion bolted on.** `migrations.js`'s
  `migrateV1ToV2` is unit-tested directly with plain arrays, but that only
  proves the backfill logic is correct in isolation -- it says nothing
  about whether `db.js`'s `onupgradeneeded` actually calls it at the right
  time relative to `store.getAll()`/`store.put()` inside a live
  versionchange transaction. `app.spec.js`'s
  `legacy v1 records (no order field) are migrated to v2 on load` seeds a
  real IndexedDB database at version 1 by hand (no `order` field, two
  records with different `createdAt`), *then* lets the real app open it,
  and asserts both the render order and that every stored record now has
  an `order` string -- the one test in this project that fails if the
  migration is wired up even slightly wrong, independent of whether the
  pure function is correct.

## Deliberate scope cuts

- **No diff/patch rendering.** `render.js` rebuilds the entire list on
  every change. `2026-08-29-js-virtual-dom` already covers *why* and *how*
  you'd build a diff layer; duplicating it here would hide the comparison
  this project is actually making (a small, static list doesn't need one)
  rather than making it clearly.
- **Undo/redo is in-memory only, not itself persisted.** Reloading the
  page loses the undo stack (though not the todos themselves) -- `History`
  is constructed fresh from whatever's in IndexedDB on every load.
- **No conflict resolution for concurrent tabs.** Two tabs open on the
  same list can both read, mutate, and write without either knowing about
  the other; `syncDb`'s before/after diff is correct for a single writer,
  not for merging two.

## What I'd add next

- **A `BroadcastChannel` listener** so two tabs open on the same todo list
  stay in sync instead of silently diverging until one of them reloads.
- **Persisted undo history**, so a reload doesn't discard how you got to
  the current state -- likely as its own IndexedDB store, since it needs
  the exact same versioning discipline `migrations.js` already has for
  the todos themselves.
- **A `by_order` index actually used for reads.** `db.js` creates it, but
  `getAll()` still fetches everything and sorts in JavaScript; a large
  list would rather cursor the index directly.
