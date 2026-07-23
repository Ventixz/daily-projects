# Game of Life (Python)

**Source:** [Project 2: The Game of Life](https://robertheaton.com/2018/07/20/project-2-game-of-life/),
from [practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

## What it is

Conway's Game of Life on an *unbounded* grid: the board is a `frozenset` of
`(row, col)` live-cell coordinates rather than a fixed 2D array, so a glider
can fly off to arbitrarily large coordinates without ever resizing anything.
`gameoflife/board.py` has the simulation core, `gameoflife/patterns.py` has
six named starting patterns (still lifes, oscillators, spaceships), and
`main.py` animates a run in the terminal. `tests/test_board.py` checks the
standard Life invariants — still lifes don't change, known oscillators
return to their start state, a glider translates diagonally by exactly
`(1, 1)` every 4 generations — rather than eyeballing ASCII frames.

## Run it

```bash
cd 2026-07-23-python-game-of-life
python3 main.py --list                                   # see available patterns
python3 main.py --pattern glider --generations 40 --fps 8 # animate one
python3 -m unittest discover -s tests -v                  # run the tests
```

```
$ python3 main.py --pattern blinker --generations 2 --fps 1000
generation 2  (population 3)

.....
..#..
..#..
..#..
.....
```

## What it actually teaches

- **An infinite board doesn't need a 2D array.** The obvious first instinct
  is a `bool[height][width]` grid, which forces a decision about board size
  before you've even picked a starting pattern, and silently clips any
  pattern that grows past the edges (a glider on a small bounded grid just
  wraps or dies at the wall, which isn't the real rule). Representing *only
  the live cells*, as a set of coordinates, sidesteps the sizing question
  entirely and matches what the rules actually care about: which cells are
  alive, not what's in some rectangle.
- **The neighbor-counting trick is what makes a sparse infinite board fast
  instead of naively-infinite.** The rules are normally stated as "for every
  cell, count its live neighbors" — but "every cell" is unbounded. Flipping
  it around — for every *live* cell, increment a counter for each of its 8
  neighbors — visits only cells within one step of something alive, via a
  single `collections.Counter`. A cell that never appears in the tally has
  zero live neighbors and is trivially excluded, so the whole next
  generation falls out of one pass over `live_cells × 8`, not one pass over
  the board.
- **The birth/survival rule collapses to one expression once you have the
  tally.** `count == 3` births a cell (dead or alive, three live neighbors
  always produces life); `count == 2 and cell in live_cells` is the only way
  a cell survives without hitting the birth threshold. Every other count —
  0, 1, 4+ — is implicitly death or continued death by not appearing in the
  output set. Writing it as a single filtered comprehension over the tally
  is what makes `next_generation` a pure function of one frozenset to
  another, with no mutation and no special-casing of "cell not in the
  tally at all" (it's just absent from the result, same as any dead cell).
- **Testing a simulation by asserting on exact future states beats
  eyeballing frames.** It would be easy to "test" this by running the
  animation and visually confirming the glider looks right, but that
  doesn't survive a refactor. Instead each oscillator test asserts the
  board returns to its exact starting `frozenset` after its known period,
  and the glider test asserts the live-cell set after 4 generations equals
  the *original* set with every coordinate shifted by `(1, 1)` — the actual
  mathematical definition of "this spaceship moved diagonally," expressed
  as a set equality instead of a screenshot.
- **`frozenset` equality gave `Board.__eq__` and the oscillator tests for
  free.** Because `next_generation` returns a `frozenset`, two boards (or
  two generations of the same board) compare equal exactly when they have
  the same live cells, regardless of insertion order — which is precisely
  the invariant "did this oscillator return to its starting state" needs,
  with no custom comparison logic.

## What I'd add next (stretch goals I skipped for scope)

- A `--random N` mode seeding a board with `N` live cells in a bounding box,
  to watch chaotic patterns settle instead of only curated ones.
- Cycle detection (hash each generation, stop early if a state repeats) so
  `--generations` isn't the only way to know a still life or oscillator has
  stabilized.
- Loading patterns from the standard `.rle` (Run Length Encoded) or
  plaintext Life 1.06 file formats, so patterns from the wider Life
  community (like real `.rle` files off LifeWiki) could be dropped in
  without hand-transcribing coordinates.
