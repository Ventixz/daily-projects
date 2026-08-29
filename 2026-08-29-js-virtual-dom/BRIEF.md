# Build Your Own Virtual DOM (JavaScript)

**Source:** ["How to write your own Virtual DOM"](https://medium.com/@deathmood/how-to-write-your-own-virtual-dom-ee74acc13060),
from the JavaScript section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
`medium.com` is outside this environment's network allowlist (same
restriction earlier days hit with `freecodecamp.org` and
`rcoh.svbtle.com`), so this is the idea the title names, built from
scratch: a `h()` function that builds a plain-object tree, a `diff`/
`patch` step that walks two such trees together, and a target to patch
against. There is also no real browser here, so the patch target is a
small hand-rolled DOM stand-in written for this project, not
`window.document`.

## What to build

- `h(tag, props, ...children)` — a hyperscript function that returns a
  plain-object virtual node (vnode): `{ tag, props, children }`. No real
  node is created here; it just describes a tree.
- A minimal "real" DOM: elements and text nodes with `appendChild`,
  `removeChild`, `replaceChild`, `setAttribute`/`removeAttribute`, since
  there's no browser to target directly.
- `createElementFromVNode(vnode)` — mounts a vnode as a real node from
  scratch (first render, and any subtree the differ decides to replace
  outright).
- `changed(a, b)` — decides whether two vnodes at the same position are
  similar enough to patch in place, or different enough to throw away and
  rebuild (different tag, different type, different text).
- `updateElement(parent, newVNode, oldVNode, index)` — the diff/patch
  core: given a real `parent` and the old/new vnode that produced its
  child at `index`, decide to append, remove, replace, or recurse into
  matching children.

## What it teaches

- A virtual DOM isn't the optimization people assume — it's a
  description format that makes "diff two trees, then mutate only what
  changed" possible in the first place. Building it from scratch is the
  only way to see where the diffing actually pays for itself vs. where a
  naive implementation just moves the cost around.
- Where a simple, unkeyed differ breaks: reordering a list is
  indistinguishable from "every item after the first change is now
  different," so the classic tutorial version (no keys) rebuilds far more
  than a keyed one would on a reorder.
- The list-diff loop has to be careful about index bookkeeping once
  mutations (insert/remove) happen mid-walk — a real trap, not a
  hypothetical one.

## Setup

- Plain Node.js, no dependencies. No `npm install`, no framework, no
  browser — everything patches against the in-repo DOM stand-in.

## Milestones

1. `h()` and a vnode shape.
2. The DOM stand-in: `Element`/`TextNode` with the handful of real-DOM
   methods the patcher needs, plus a `toHTML` serializer for tests.
3. `createElementFromVNode` — mount any vnode from scratch.
4. `changed()` + `updateElement()` — the diff/patch core, recursing into
   children and reconciling props.
5. A demo (`src/main.js`) that renders a small "app," re-renders it a few
   times with different data, and patches each new tree onto the same
   real root.
6. A test suite covering append/remove/replace/recurse and both a
   shrinking and a growing child list.
