# Tetris in ClojureScript (Clojure/ClojureScript)

**Source:** ["Tetris in ClojureScript"](https://shaunlebron.github.io/t3tr0s-slides),
from the Clojure section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The original (`t3tr0s`) is a full multiplayer, server-synced game built
on Om/React and core.async channels. In keeping with every other
"build it yourself" entry in this repo, I kept the part the slides are
actually teaching -- a single-player game loop driven by a pure,
immutable game-state function, rendered to a canvas -- and dropped the
multiplayer transport, React, and core.async entirely. `tetris.core` has
zero dependencies beyond `clojure.core`, on either runtime.

There's no wall-kick table: a rotation that doesn't fit is rejected
outright rather than nudged sideways to make room, which is a real
(and noticeable, if you know Tetris) simplification of the SRS rotation
system. That's a deliberate scope cut, not an oversight -- see "Scope
cuts" below.

## What it is

- `src/tetris/core.cljc` -- the entire game: board, all 7 tetrominoes
  (as raw `[row col]` cell offsets per rotation, no matrix math), movement,
  naive rotation, gravity, line-clearing, and scoring. `.cljc`, not
  `.cljs`, so it compiles under **both** JVM Clojure and ClojureScript --
  the same functions run the browser game and the test suite. Every
  function here is pure: `(board, piece) -> board, piece`, nothing
  reaches for global state, a DOM node, or randomness (except
  `random-kind`, and every function that needs a new piece takes the
  *result* of calling it as an explicit argument instead of calling it
  itself -- so a test can hand `tick`/`drop-hard` a fixed `:next-kind`
  and get a deterministic answer).
- `src/tetris/ui.cljs` -- the only file that touches the DOM: canvas
  rendering, `keydown` handling, and a `requestAnimationFrame` loop that
  calls `tetris.core/tick` on a timer that speeds up with `:level`. Holds
  exactly one piece of mutable state, `(defonce state (atom ...))`, and
  every keypress is `(swap! state core/some-fn ...)` -- the atom update
  and the pure function are two different things on purpose.
- `src/tetris/build.clj` -- compiles `ui.cljs` straight through
  `cljs.build.api`, not the `lein-cljsbuild` plugin (see "Scope cuts").
- `test/tetris/core_test.clj` -- 17 `clojure.test` tests, all against
  `tetris.core` directly on the JVM. No browser, no ClojureScript
  compile step, sub-second run.
- `e2e/smoke.mjs` -- a Playwright script that loads the *compiled* page
  in real headless Chromium and drives it with real `keydown` events,
  the one check that `ui.cljs`'s DOM/canvas/event wiring actually works
  end to end, which the JVM test suite structurally can't cover.

## Why it's worth building

- **One state shape, two runtimes, no adapter.** `tetris.core.cljc`
  never imports anything runtime-specific, so the identical file is
  compiled by `javac`+`clojure.lang.Compiler` for the test suite and by
  the ClojureScript/Closure compiler for the browser bundle. The thing
  that makes this possible isn't a language feature so much as a
  discipline: the moment `core.cljc` touched `js/Math.random` or
  `System/currentTimeMillis` directly, the `.cljc` split would break.
  Pushing randomness out to a caller-supplied argument (`next-kind`) is
  what keeps it portable, not a reader-conditional.
- **The render function is a fold over an atom, not a mutator.**
  `board+piece` never touches `@state` -- it's called with a snapshot
  and returns a new board with the falling piece painted on top for
  drawing only, which is then thrown away. Nothing in `tetris.core` ever
  needs to know a piece can be "in flight, about to be rendered but not
  locked" -- that's a rendering concern that `ui.cljs` invents and
  discards on every frame, keeping `core.cljc`'s state shape simple
  (there's exactly one piece: `:current`, full stop).
- **`(swap! state core/tick next-kind)` is the whole event loop.** Every
  key handler and the gravity timer funnel through `swap!` calling a
  `core.cljc` function; none of them special-case what changed. That's
  what a `reduce`-shaped game loop buys you: `ui.cljs` doesn't
  distinguish "the user pressed left" from "gravity ticked" from "the
  user hard-dropped" -- they're all just `(swap! state f args...)` for
  different `f`.

## Scope cuts (and why)

- **No wall kicks.** Real Tetris (the SRS standard) tries several offset
  positions before rejecting a rotation, so pieces can rotate right up
  against a wall or another piece. `try-rotate` here only tries the
  piece's current position -- reject if it doesn't fit. Implementing the
  full kick table is mechanical (it's just more data), but it's a lot of
  data for what this project is teaching, which is the reactive-render
  loop, not SRS compliance.
- **No `lein-cljsbuild`, no `figwheel`, no hot-reload dev server.** Those
  are the tools the original tutorial actually uses, and all of them are
  Clojars-only packages. This sandbox's egress proxy allows Maven
  Central but returns `403` for `repo.clojars.org` -- confirmed by
  trying to resolve `lein`'s own default `nrepl` dependency, which is
  Clojars-only and isn't part of this project's own `:dependencies` at
  all. `org.clojure/clojurescript` itself is fully mirrored on Central,
  so `cljs.build.api` (called directly from `tetris.build/-main`, no
  plugin layer) compiles the whole thing with zero Clojars traffic.
  Recompiling on every save instead of hot-reloading is the cost; it's
  a `lein run` away.
- **`rand-nth` for the next piece, not the standard "7-bag" randomizer**
  (which deals all 7 pieces once each before repeating, so you're never
  more than 12 pieces from seeing every shape). Plain uniform random can
  hand you three `S` pieces in a row; noted as a stretch goal below
  rather than built, since it's a pure function change to `random-kind`
  callers and doesn't touch the interesting parts of this project.

## Setup (Windows 11 / PowerShell)

```powershell
# Java (Leiningen needs a JDK on PATH)
winget install --id Microsoft.OpenJDK.21 -e
# Leiningen: download the batch script, then let it self-install on first run
Invoke-WebRequest https://raw.githubusercontent.com/technomancy/leiningen/stable/bin/lein.ps1 -OutFile lein.ps1
.\lein.ps1 version
# Node (for the Playwright e2e check and a static file server) and Playwright's browser
winget install --id OpenJS.NodeJS.LTS -e
npm install
npx playwright install chromium
```

## Build it

```
lein run
```

Compiles `src/tetris/ui.cljs` (and the `tetris.core` it requires)
straight to `resources/public/js/main.js` via `cljs.build.api` --
`Makefile`'s `build` target does the same thing.

## Run it

```
make run
```

Serves `resources/public/` on `http://localhost:8080` -- open it and
play. Arrow keys move/rotate/soft-drop, Space hard-drops, `P` pauses.

## Test it

```
make test    # 17 clojure.test tests over tetris.core, JVM only, no browser
make e2e     # builds, serves the compiled page, and drives it with Playwright
```

`make e2e` needs `npm install` once (installs `playwright` and
`http-server` as dev dependencies) and, the first time, `npx playwright
install chromium` to fetch the browser binary.

## Stretch goals (not implemented)

- SRS wall kicks, so rotation near walls/stacks behaves like real Tetris.
- 7-bag randomizer instead of `rand-nth` for the next piece.
- Hold-piece slot and a ghost-piece preview at the hard-drop landing spot.
- `localStorage` high score, since the whole game already lives in one
  atom that's trivial to serialize.

## Hints

- `piece-defs`' rotation cells are offsets inside a small bounding box,
  not normalized to start at `[0 0]` -- the `O` piece's cells start at
  column offset `1`, not `0`, so its leftmost reachable board column is
  `-1`, not `0`. `valid-position?` handles this fine since it checks
  *absolute* cells, but a test (or a new caller) that assumes a piece's
  `:col` is itself a board column will be off by one. Check
  `absolute-cells`, not `:col`, when you want to know where a piece
  actually is.
- `lein`'s very first run on a fresh install (`lein version`, or any
  other task) unconditionally tries to resolve `nrepl`/`nrepl` and
  `org.nrepl/incomplete` regardless of what's in this project's own
  `:dependencies` -- it's baked into Leiningen's own default profile
  merge, not something `project.clj` can opt out of. If Clojars is
  unreachable, that first run fails before it ever reads this project's
  own deps. It doesn't need to actually work (nothing here uses `lein
  repl`), just to resolve, so a stub `.pom`/`.jar` pair dropped into the
  local `~/.m2` repository at the expected coordinates is enough to get
  past it.
- ClojureScript's `:simple` optimization level still runs the Google
  Closure Compiler, which is a several-hundred-KB dependency in its own
  right (`com.google.javascript/closure-compiler-unshaded`) -- that's
  most of the size of the compiled `main.js`, not this project's ~250
  lines of application code.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Tutorial: ["Tetris in ClojureScript"](https://shaunlebron.github.io/t3tr0s-slides) by Shaun LeBron.
