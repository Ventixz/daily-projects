# Build Your Own Virtual DOM (JavaScript)

**Source:** ["How to write your own Virtual DOM"](https://medium.com/@deathmood/how-to-write-your-own-virtual-dom-ee74acc13060),
from the JavaScript section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
`medium.com` isn't reachable from this environment (same allowlist
restriction earlier days hit with `freecodecamp.org` and
`rcoh.svbtle.com`), so what's here isn't a port of that article's code —
it's the idea its title names, built from scratch: a hyperscript builder,
a diff/patch step, and a target for the patches to land on.

## What it is

- `src/dom.js` — a minimal DOM stand-in (`Element`, `TextNode`,
  `appendChild`/`removeChild`/`replaceChild`/`insertChildAt`,
  `setAttribute`/`removeAttribute`, plus a `toHTML` serializer for tests
  and the demo). There's no browser in this sandbox, so the patcher
  targets this instead of `window.document` — same node shape, small
  enough to read in one sitting.
- `src/h.js` — `h(tag, props, ...children)`, ~10 lines, returns a plain
  object `{ tag, props, children }`. Pure; touches nothing real.
- `src/render.js` — `createElementFromVNode(vnode)`: mounts a vnode as a
  real node from scratch. Used both for first render and for any subtree
  the differ decides to throw away and rebuild.
- `src/diff.js` — the core: `changed(a, b)` decides same-position
  similarity; `updateElement(parent, newVNode, oldVNode, index)` walks
  two vnode trees together and mutates `parent` to match `newVNode`,
  reusing what it can.
- `src/main.js` — a tiny counter "app," re-rendered from scratch five
  times and patched onto the same real root each tick, printing the
  resulting tree via `toHTML` after every patch.
- `test/test.js` — 15 hand-rolled checks: hyperscript → real tree,
  `changed()`'s four cases, append/remove/replace/recurse, prop
  add/change/remove, a shrinking list, a growing list, and a full
  multi-tick integration run.

## Run it

```bash
cd 2026-08-29-js-virtual-dom
make test   # 15 checks
make run    # prints the tree after each of 5 patches to a counter app
```

## What it actually teaches

- **A virtual DOM is a description format before it's an optimization.**
  `h()` never touches a real node — it just builds `{ tag, props,
  children }`. The entire value of that indirection is that
  `updateElement` can compare *two* such descriptions and decide what
  changed *before* touching anything real. Skipping the vnode step and
  diffing real nodes directly is possible in principle, but it's exactly
  what makes "did this subtree change" cheap to answer here: comparing
  plain objects, not live DOM state.
- **Reordering a list is indistinguishable from "everything changed"
  once there are no keys.** `changed()` only looks at position and tag —
  it has no way to recognize that the `<li>b</li>` now at index 0 is the
  *same* logical item that used to be at index 1. `test/test.js`'s
  "recurses into unchanged-tag children" check deliberately keeps items
  in the same order to show the reuse case working (`el.children[0]` is
  asserted to be the *same object* before and after, not rebuilt); a
  reorder test would show the opposite — same tag at the new position,
  so `changed()` says "no," and the differ patches Y's old node into
  looking like Y's neighbor's new content instead of moving anything.
  That's the entire reason React-style vdoms accept a `key` prop.
- **The list-diff loop's iteration order is not a style choice — it's
  the difference between a passing and a silently-wrong growing list.**
  The first version of `updateElement` walked children front-to-back,
  which is the classic tutorial's own approach. Growing an old list of
  `[a]` to a new list of `[a, b, c]` sends `updateElement` two "no old
  node, append" calls, one for index 1 (`b`) and one for index 2 (`c`).
  Iterating front-to-back and using a blind `appendChild` looked
  right — and was, for that specific case. The bug showed up once I
  switched to iterating **back-to-front** (to make *removals* safe: a
  `removeChild` at a higher index can't invalidate a lower, not-yet-visited
  index if you never revisit it after). Back-to-front processes index 2
  (`c`) before index 1 (`b`), and a plain `appendChild` always lands at
  the *current* end of the array regardless of which logical index asked
  for it — so `c` landed before `b`. `test/test.js`'s "growing child list
  appends new real nodes" check caught this immediately (`<ul><li>a</li><li>c</li><li>b</li></ul>`
  instead of `...a, b, c...`) the first time `make test` ran. The fix
  was `Element.insertChildAt(node, index)` (an `Array.splice(index, 0,
  node)`) in place of `appendChild` in the "no old node" branch of
  `updateElement` — insertion by *position*, not by "whatever's currently
  last." Both bugs (front-to-back's stale post-removal indices, and
  naive back-to-front's reversed appends) come from the same root cause:
  a list-diff has to pick, deliberately, whether it's safe for removal or
  safe for append, and a `push`-only `appendChild` is only safe for one
  of those two directions.
- **`changed()`'s four branches earn their keep individually.** Tag
  mismatch, text-content mismatch, and type mismatch (text vs. element)
  all have to trigger a full rebuild for different reasons — a `<div>`
  becoming a `<span>` can't be patched (different attrs, different
  semantics), but two different plain strings *could* in principle be
  patched by just updating `textContent`. This implementation doesn't
  bother distinguishing that last case from a full replace (see scope
  cuts below), but writing `changed()`'s four assertions separately in
  `test/test.js` is what made it obvious there were four genuinely
  different cases, not one.

## Deliberate scope cuts

- **No keys, no move detection.** As above — reordering is handled as
  "diff every position," which is correct but does more rebuilding than
  necessary. Keyed diffing (map old/new children by key, detect moves,
  patch only real changes) is the natural next step and is *the*
  well-known limitation of this exact tutorial's algorithm.
- **Text updates replace the text node instead of patching
  `textContent` in place.** `changed('a', 'b')` returns `true`, so
  `updateElement` calls `replaceChild` with a whole new `TextNode`
  rather than mutating `textContent` on the existing one. Simpler, and
  behaviorally identical from the outside (same result in `toHTML`), but
  a real virtual DOM would patch text nodes in place to avoid the
  allocation.
- **No component model, no event handling, no lifecycle hooks.** This is
  the diff/patch primitive a framework would be built on top of, not the
  framework — `props` can hold arbitrary key/value pairs (attributes),
  but nothing here calls a function when one changes.
- **No real browser.** The patch target is `src/dom.js`'s stand-in, not
  `window.document`. It's deliberately shaped like the real DOM API
  (`appendChild`/`removeChild`/`replaceChild`/`setAttribute`) so the
  diff/patch logic in `src/diff.js` would need no changes to run against
  an actual browser — only `src/render.js`'s `dom.createElement` calls
  would need to become `document.createElement` calls.

## What I'd add next

- **Keyed list diffing.** Give `h()`'s vnodes an optional `key` in
  `props`, and have the children loop in `updateElement` build old/new
  key maps first, so a reorder produces `insertChildAt`/move operations
  instead of N rebuilt positions.
- **In-place text patching.** Special-case `isText(newNode) &&
  isText(oldNode) && newNode !== oldNode` to mutate `textContent` rather
  than routing through `changed()`'s replace path.
- **A `render(vnode, realDOMTarget)` adapter over `window.document`**,
  to prove the "only `render.js` needs to change" claim above against an
  actual browser (or `jsdom`) rather than just asserting it in prose.
