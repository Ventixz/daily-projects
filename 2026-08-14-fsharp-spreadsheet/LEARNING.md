# Write your own Excel in 100 lines of F# (F#)

**Source:** ["Write your own Excel in 100 lines of F#"](http://tomasp.net/blog/2018/write-your-own-excel),
from the F# section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
I couldn't reach the tutorial itself from this sandbox (its domain is
blocked by the network egress proxy this environment runs behind), so I
kept the brief -- a small spreadsheet engine: parse formulas, build a
dependency graph between cells, evaluate it, recompute correctly when a
cell changes -- and designed my own implementation rather than following
the original post's structure line for line.

## What it is

A console spreadsheet engine, not a UI: cells hold a number, text, or a
formula (`=A1+B2*2`, `=SUM(A1:A4)`), and editing one cell recomputes only
the cells that actually depend on it, in the right order.

- `src/Types.fs` -- `CellRef` (a struct with hand-written structural
  equality/comparison, since it's the key type for every dictionary and
  set in the project), `Expr` (the formula AST), `CellContent` (what's
  typed into a cell), `CellValue` (what it evaluates to: number, text,
  blank, or an error string like `#DIV/0!`).
- `src/ColumnCodec.fs` -- converts between `0` and `"A"`, `26` and
  `"AA"`, etc. Spreadsheet columns are *bijective* base-26, not ordinary
  base-26, and the two are easy to conflate -- see below.
- `src/Lexer.fs` / `src/Parser.fs` -- hand-written tokenizer and
  recursive-descent parser, `+ - * / ^`, parens, unary minus, string and
  number literals, cell references, ranges (`A1:B3`), and function calls
  (`SUM`, `AVERAGE`, `MIN`, `MAX`, `COUNT`).
- `src/Deps.fs` -- walks an `Expr` to collect the set of cells it reads,
  expanding ranges into individual cell references.
- `src/Eval.fs` -- evaluates one cell's `Expr` given a `lookup: CellRef ->
  CellValue` for its neighbors. Never recurses into another cell's
  formula -- see "what it teaches" below for why that distinction matters.
- `src/Graph.fs` -- `affectedSet` (BFS over "who depends on me" edges) and
  `topoOrder` (Kahn's algorithm, restricted to a subset of cells, doubling
  as cycle detection).
- `src/Sheet.fs` -- the mutable heart of it: a `Dictionary<CellRef,
  CellRecord>`, `SetCell` that reparses, rewires the dependency graph, and
  recalculates only what changed.
- `cli/` -- loads a `.sheet` text file, prints it as a grid, edits one
  cell, and prints the grid again to show the recalculation.
- `tests/` -- 1,608 assertions (most of them a round-trip loop over 1,500
  column indices), hand-rolled runner, no test framework dependency, same
  pattern as this repo's other C#/Java projects.

## Run it

```bash
cd 2026-08-14-fsharp-spreadsheet
make test                              # 1,608 assertions, no external deps
make run                                # loads examples/budget.sheet, edits a cell, shows the diff
make repl                               # interactive: set A1 5 | get A1 | print | quit
```

## What it actually teaches

- **Spreadsheet columns are bijective base-26, and the difference from
  ordinary base-26 shows up exactly at the two-letter boundary.** In
  ordinary base-26 there's a digit for zero, so counting up from `Z`
  (25) looks like `Z, BA, BB, ...` the same way decimal rolls `9` over to
  `10`. But this alphabet has no zero -- `A` is 1, not 0 -- so column 26
  is `AA`, not `BA`: the leftmost "digit" doesn't reset when it would
  need to represent a leading zero, because that digit doesn't exist. My
  first version of `colToLetters` wrote the standard base-26 loop
  (`n % 26`, `n / 26`) and got `Z` right but produced garbage from `AA`
  onward -- fixed by subtracting 1 before extracting each digit
  (`(n - 1) % 26`, then `(n - 1) / 26`), which is what makes the encoding
  bijective. `ColumnCodecTests.run` pins both the specific boundary
  values (`25 -> "Z"`, `26 -> "AA"`, `51 -> "AZ"`, `52 -> "BA"`, `701 ->
  "ZZ"`, `702 -> "AAA"`) and a round trip across every column 0 through
  1500, so a regression at any boundary -- not just the one I originally
  hit -- fails loudly.
- **`Eval.evalExpr` reads a neighbor's cached `CellValue`, never its
  `Expr`.** It would be easy to write `ERef r -> evalExpr lookup
  (sheet.GetContent r formula)` -- evaluate the formula behind the
  reference, recursively -- and it would even produce correct answers on
  simple sheets. It falls over on two different shapes of graph: a
  diamond (two cells that both read a third, then a fourth that reads
  both of them) gets the shared cell re-evaluated once per path to it,
  which is merely wasteful on a small sheet and exponential on a deep
  lattice of diamonds; a cycle turns into unbounded recursion with no
  base case, since nothing is tracking "already visiting this cell."
  `Sheet.Recalculate` sidesteps both: `Graph.topoOrder` computes a safe
  evaluation order first, `Eval.evalExpr`'s `lookup` is just a dictionary
  read of whatever's already been written for that cell in this pass, and
  cycles are caught by `topoOrder` before `Eval` ever runs. `SheetTests`
  builds an actual diamond (`D1` reads `B1` and `C1`, both of which read
  `A1`) and asserts editing `A1` costs exactly 4 evaluations, not 5 --
  proving `D1` (and every other cell in the diamond) is computed once,
  not once per incoming edge.
- **A dependency graph has two directions, and only maintaining one of
  them silently breaks incremental recalculation.** `SetCell` needs `Deps`
  (what a cell reads, used to order evaluation) and `Dependents` (who
  reads a cell, used to find what to recompute -- the reverse edge). The
  bug I actually hit while building this: when a formula is *replaced* by
  something that no longer references a given cell, the old reverse edge
  has to be explicitly torn down, or the abandoned cell keeps getting
  recomputed forever even though its new formula never reads the thing
  that changed. `SetCell` computes `Set.difference oldDeps newDeps` and
  removes `r` from each dropped dependency's `Dependents` before adding
  the new edges. `SheetTests` catches the un-fixed version directly: it
  points `B1` at `A1`, then repoints it to a constant, then edits `A1`
  again and asserts the recompute count is exactly 3 (`A1`, `C1`, `D1`)
  rather than 4 -- if the stale edge from `A1` to `B1` were left in place,
  `B1` would recompute too, uselessly, every time `A1` changes for the
  rest of the sheet's life.
- **A cell reading from a cycle is just as unorderable as the cycle
  itself, and Kahn's algorithm gets this for free if you let it.**
  `topoOrder`'s cycle set isn't "nodes I detected a back-edge from" -- it's
  "nodes whose in-degree never reached zero," which naturally includes
  anything (inside the recomputed subset) that depends, even indirectly,
  on a cyclic cell, because its in-degree can't clear until its cyclic
  input's in-degree clears, which never happens. `GraphTests` has a
  `downstreamOfCycleDeps` case (`c` reads `b`, `b` and `a` cycle on each
  other) asserting all three end up in `Cycle`, and `SheetTests` mirrors
  it at the `Sheet` level with a `W1 = X1 + Y1` reading from an `X1`/`Y1`
  cycle, checking it also shows `#CYCLE!` rather than some other error or
  a stale value.

## Deliberate scope cuts

- **No `IF` or comparison operators.** Arithmetic plus five aggregate
  functions is enough to exercise parsing, the dependency graph, and
  incremental recalculation -- the three things this project is actually
  about. Conditional formulas would mean deciding how truthiness and
  comparison chain (`A1 < B1 < C1`?) works, which is a different, mostly
  unrelated design problem.
- **Errors don't distinguish "I am on a cycle" from "I merely read from
  one."** Both report `#CYCLE!`. A real spreadsheet's "circular reference"
  warning usually only flags the actual cycle; this one flags the whole
  unorderable subgraph identically, which is simpler and, for a learning
  project, not meaningfully less useful.
- **`COUNT` propagates errors from its range, real spreadsheets' doesn't.**
  Excel's `COUNT` silently ignores everything that isn't a number,
  including error cells, while this project's `SUM`/`AVERAGE`/`MIN`/`MAX`/
  `COUNT` all short-circuit on the first error found in a range, for one
  consistent rule instead of a function-by-function special case.
- **No cell formatting, no saving edits back to the `.sheet` file, no
  undo.** The CLI is a demonstration harness for the engine, not an
  editor.

## What I'd add next

- **String concatenation and comparison operators**, which would need a
  real notion of "truthy" for `IF` to sit on top of.
- **A `#REF!` error for the case a real spreadsheet handles specially:
  deleting a row or column that other formulas point at.** This project
  never removes rows/columns, only edits individual cells, so that
  failure mode doesn't currently exist to guard against.
- **Persisting edits**, so the REPL's `set` commands could be saved back
  out to a `.sheet` file instead of only living in memory for the process.
