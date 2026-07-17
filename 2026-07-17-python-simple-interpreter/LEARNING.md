# Simple Interpreter — Pascal Subset (Python)

**Source:** [Let's Build A Simple Interpreter](https://ruslanspivak.com/lsbasi-part1/), from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting rather than scaffolded-then-attempted, so this folder
contains the finished implementation directly at the project root (no separate `reference/`).

## What it is

A tree-walking interpreter for a subset of Pascal, written against only Python's standard
library. It runs a `.pas` file through four stages — lexer, parser, semantic analyzer,
interpreter — and prints the final values of every global variable. The subset covers `PROGRAM`
headers, `VAR` declarations with `INTEGER`/`REAL` types, `BEGIN...END` compound statements,
assignment, arithmetic with correct operator precedence (`+ -` looser than `* / DIV`), unary
`+`/`-`, parenthesized expressions, and `{ curly-brace }` comments. It deliberately stops short of
control flow (`IF`, loops) and procedures — see "what I'd add next" below.

## Run it

```powershell
# Windows 11 / PowerShell
winget install Python.Python.3.12
python --version   # confirm the toolchain installed

cd 2026-07-17-python-simple-interpreter
python -m pascal_interpreter examples\part10.pas
```

```bash
# macOS/Linux
cd 2026-07-17-python-simple-interpreter
python3 -m pascal_interpreter examples/part10.pas
```

```
Final global scope:
  a = 2
  b = 25
  number = 2
  y = 5.997142857142857
```

Try `examples/precedence.pas` to see operator precedence and parentheses at work, or
`examples/undeclared_var.pas` to see semantic analysis reject a program before it runs a single
statement:

```
$ python3 -m pascal_interpreter examples/undeclared_var.pas
SemanticError: Assignment to undeclared variable 'y'
```

Run the test suite with `python3 -m unittest discover -s tests -v` (24 tests, no dependencies
beyond the standard library).

## What it actually teaches

- **The lexer/parser/interpreter pipeline is just three small, composable passes.** `Lexer`
  turns raw text into a flat `Token` stream; `Parser` is recursive descent — one method per
  grammar production (`expr`, `term`, `factor`, ...) — and builds an AST; `Interpreter` walks that
  AST. Once the AST exists, evaluating it and *type-checking* it (see next point) are two
  independent, swappable passes over the same tree.
- **Operator precedence comes from grammar shape, not from special-casing operators.**
  `expr` calls `term` calls `factor`, so `*`/`/`/`DIV` naturally bind tighter than `+`/`-` — no
  precedence table or Pratt parsing needed for a grammar this small. Left-associativity
  (`10 - 2 - 3 == 5`, not `10`) falls out of the `while` loop in `expr`/`term` building a
  left-leaning `BinOp` chain, rather than recursing on the left operand.
- **The visitor pattern lets one AST serve two unrelated passes.** `SemanticAnalyzer` and
  `Interpreter` both subclass the same `NodeVisitor` (`visit(node)` dispatches to
  `visit_<ClassName>`) and walk identical trees, but one builds a symbol table and raises on
  undeclared names while the other computes values. Neither needs to know the other exists —
  that separation is what makes "catch the bug before running anything" possible at all.
- **Catching errors at the right stage matters more than catching them at all.** A semantic pass
  that runs *before* interpretation means `x := undeclared_var` is rejected even if the line
  before it would have divided by zero — the program never partially executes. That is a
  meaningfully different guarantee than a dynamically-typed interpreter that fails mid-run.
- **A grammar written as a docstring is executable documentation.** Keeping the EBNF grammar
  comment at the top of `parser.py` in lockstep with the methods below it means the file reads
  as its own spec — deviations between the comment and the code are exactly the bugs worth
  catching in review.

## What I'd add next (stretch goals I skipped for scope)

- `IF/THEN/ELSE` and a `WHILE` loop — needs control-flow nodes in the AST and a
  visit-with-branching interpreter, plus loop-condition tests (including "body never runs").
- `PROCEDURE` declarations with parameters — the natural next step in Spivak's series, and the
  point where the symbol table needs actual *scoping* (nested scopes with a parent pointer)
  instead of one flat global table.
- A `PRINT` builtin so programs can produce output directly instead of only being inspectable via
  the final global-scope dump.
