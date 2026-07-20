# Pascal-Subset Interpreter (Python)

**Source:** [Build a Simple Interpreter](https://ruslanspivak.com/lsbasi-part1/), from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting rather than scaffolded-then-attempted, so this folder
contains the finished implementation directly at the project root (no separate `reference/`). Go
was the last language used (2026-07-15), so this one is Python, per the 7-day no-repeat rule in
`HISTORY.md`.

## What it is

A tree-walking interpreter, written against only Python's standard library, for a small
Pascal-like language: `PROGRAM`/`VAR`/`BEGIN`/`END`, typed variable declarations (`INTEGER`,
`REAL`), assignment (`:=`), arithmetic with correct precedence, `IF`/`THEN`/`ELSE`, `WHILE`/`DO`,
and a `PRINT` statement. Source goes in one end (`.pas` text) and out the other comes either
program output or an error with a line and column number.

The pipeline is the classic four stages, each its own module:

```
source text -> Lexer -> tokens -> Parser -> AST -> SemanticAnalyzer -> (checked AST)
                                                                             |
                                                                             v
                                                                       Interpreter -> output
```

## Run it

```bash
python3 -m pascal_interpreter examples/factorial.pas   # -> 3628800
python3 -m pascal_interpreter examples/fizzbuzz.pas     # -> 1 2 Fizz 4 Buzz Fizz ... FizzBuzz
```

Run the test suite (needs `pytest`; `pip install pytest` if it's not already on the machine):

```bash
python3 -m pytest -q
```

## What it actually teaches

- **Why parsing happens in layers.** `expr -> simple_expr -> term -> factor` isn't arbitrary
  nesting — it's how operator precedence gets encoded into a recursive-descent parser without a
  precedence table. `2 + 3 * 4` parses correctly as `2 + (3 * 4)` purely because `term()` (which
  handles `*`) sits one level below `simple_expr()` (which handles `+`), so a `*` always ends up
  nested *inside* the `+` node built one level up. Get the layering wrong and precedence silently
  breaks.
- **The visitor pattern as a dispatch mechanism, not just a GoF diagram.** Both `SemanticAnalyzer`
  and `Interpreter` need "do something different per AST node type" — `NodeVisitor.visit()` does
  that via `getattr(self, "visit_" + type(node).__name__)` instead of a 15-branch `if/elif` chain.
  Adding a new statement type (I added `IfStatement`/`WhileStatement`/`PrintStatement` beyond the
  textbook's arithmetic-only version) means adding one `visit_X` method to each pass, not editing
  a dispatcher.
- **Semantic analysis as a separate pass from evaluation.** Catching `x := 1;` for an undeclared
  `x` *before* running anything (via a `ScopedSymbolTable` walked in a dedicated pass) is what
  makes it a checked error with a source position, rather than a Python `KeyError` surfacing from
  three call frames deep inside `Interpreter.visit_Var`. Splitting "does this program make sense"
  from "what does this program do" is the same shape real compilers use.
- **Precise error positions require carrying them from token 1.** Every `Token` stores its own
  `line`/`column`; every AST node keeps the token it was built from. That's what turns `division
  by zero` into `division by zero (line 4, col 19)` — the position isn't reconstructed after the
  fact, it's threaded through the whole pipeline from the lexer onward.
- **`DIV`/`MOD`/`/` are three different operators on purpose.** Real Pascal (and this subset)
  separates integer division (`DIV`), remainder (`MOD`), and float division (`/`) as distinct
  tokens rather than overloading `/` the way Python 2 or C do — it's a deliberate design choice
  that avoids a whole class of "did I mean int or float division" bugs at the language level
  instead of the programmer's.

## What I'd add next (stretch goals I skipped for scope)

- Procedure/function declarations with their own scopes (the symbol table already supports
  `enclosing_scope` chaining — procedures are the reason that field exists, even though only the
  global scope uses it right now).
- Arrays and `FOR` loops, which would need a new `Type` variant and a bounds-checked memory model.
- A REPL mode for interactive one-liners instead of only whole-file execution.
