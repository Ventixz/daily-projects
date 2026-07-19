# Simple Expression Interpreter (Python)

**Source:** [Miscellaneous → "Build a Simple Interpreter"](https://ruslanspivak.com/lsbasi-part1/), from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting rather than scaffolded-then-attempted, so this folder
contains the finished implementation directly at the project root (no separate `reference/`).

## What it is

A small language for arithmetic and variables, implemented the way real compilers and interpreters
are structured: a **lexer** turns source text into tokens, a **parser** turns tokens into an AST
respecting operator precedence, and a tree-walking **interpreter** evaluates the AST against a
variable environment. `main.py` wraps that pipeline in both a REPL and a script runner:

```bash
python3 main.py                    # interactive REPL
python3 main.py examples/sample.calc   # run a script, one result printed per line
```

```
calc> x = 5
5
calc> x * 2 + 1
11
calc> 1 / 0
error: Division by zero
```

Supports: integers and floats, `+ - * /` with correct precedence, parentheses, unary `+`/`-`
(including chains like `--5`), and variable assignment/reference that persists across lines because
the REPL keeps one `Interpreter` (and its environment dict) alive for the whole session.

Run the test suite with `python3 -m pytest` (32 tests across the lexer, parser, and interpreter).

## What it actually teaches

- **Why lexing and parsing are separate passes.** The lexer only ever answers "what's the next
  token," with no notion of grammar; the parser only ever asks the lexer for tokens, with no notion
  of characters. Keeping the boundary strict is what makes each half independently testable —
  `test_lexer.py` never touches the AST, `test_parser.py` never evaluates anything.
- **Precedence climbing without a precedence table.** The classic trick for turning EBNF into a
  recursive-descent parser: `expr` calls `term` calls `factor`, and each level only knows about
  its own operators (`+ -` vs `* /`). Precedence falls out of the *call structure*, not a table of
  numbers — `2 + 3 * 4` parses correctly because `term()` fully consumes `3 * 4` as one unit before
  `expr()` ever sees the `+`. `test_binop_precedence_shape` asserts the resulting tree shape
  directly to make this concrete.
- **Left-associativity is a loop, not recursion.** `expr()` and `term()` use a `while` loop
  (`node = BinOp(node, op, next())`) rather than recursing on the left side. Recursing left would
  build `1 - (2 - 3)`; the loop builds `(1 - 2) - 3` — those disagree (`2` vs `-4`), and only the
  loop form matches how `-` actually associates.
- **One token of lookahead is sometimes unavoidable.** `x = 1` (assignment) and `x + 1` (expression)
  are indistinguishable at the first token — both start with `IDENTIFIER`. The parser peeks one
  token ahead by snapshotting the lexer's `(pos, current_char)`, calling `get_next_token()`, then
  restoring — a small, contained way to get lookahead without turning the lexer into a buffered
  stream.
- **The interpreter pattern (`visit_<NodeType>`).** Each AST node type gets its own `visit_*`
  method, dispatched by class name via `getattr`. Adding a new node kind (say, a `Pow` node for
  exponentiation) means adding one `visit_Pow` method — nothing else in the interpreter changes.
  This is the same shape as `ast.NodeVisitor` in Python's own standard library.
- **Where state lives determines what "session" means.** The `Environment` dict is passed into
  `Interpreter.__init__` rather than being a module global, specifically so `test_separate_
  interpreters_do_not_share_state` can hold: two independent REPL sessions (or two script runs)
  must not see each other's variables.

## What I'd add next (stretch goals I skipped for scope)

- Comparison operators and an `if`/`else` construct — needs a `Bool` value type and control-flow
  AST nodes, but the visitor dispatch already generalizes to that.
- Multi-line constructs (blocks, loops) — the current one-line-per-statement design (one `Lexer`
  per line) would need to become a single lexer over the whole file with an explicit `NEWLINE`
  token, closer to how the source tutorial's later parts handle full Pascal programs.
- Named functions with parameters — would need a call-stack of environments instead of the single
  shared dict, so a function's locals don't leak into (or collide with) the caller's.
