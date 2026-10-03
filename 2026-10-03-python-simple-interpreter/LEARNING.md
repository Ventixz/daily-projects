# Learning: A Pascal-subset Interpreter in Python

**Source:** ["Let's Build A Simple Interpreter"](https://ruslanspivak.com/lsbasi-part1/)
from the Python section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Pure standard library, a single `interpreter.py`. It follows the series' arc
(calculator -> parser -> Pascal programs with variables), compressed into one file.

## What it is

- **Lexer** — turns source text into tokens; handles `{ comments }`, `:=`,
  integer/real literals, and case-insensitive keywords and identifiers.
- **Parser** — recursive descent, one method per grammar rule, producing an AST of dataclasses.
- **Interpreter** — a tree-walking visitor (`visit` dispatches to `v_<NodeClass>`).
  Checks declarations, types, and runtime errors.
- `example.pas` is the tutorial's reference program; `test_interpreter.py` has 9 tests.

Grammar:

```
program   : PROGRAM ID SEMI block DOT
block     : (VAR (ID (COMMA ID)* COLON type SEMI)+)? compound
compound  : BEGIN statement (SEMI statement)* END
statement : compound | ID ASSIGN expr | empty
expr      : term ((PLUS | MINUS) term)*
term      : factor ((MUL | DIV | FLOAT_DIV) factor)*
factor    : (PLUS | MINUS) factor | number | LPAREN expr RPAREN | ID
```

## What I learned

- **Grammar rule = parser method.** Operator precedence comes from nesting
  (`expr` -> `term` -> `factor`), and left associativity from the `while` loop.
  `10 - 3 - 2` is 5 only because the loop folds left.
- **Lexing vs. parsing is a clean cut.** The lexer needs one character of
  lookahead (`:` vs `:=`). The parser needs one token.
- **Unary operators bind tighter than binary ones**, which makes `5 - - 3` parse naturally via a recursive `factor`.
- **Visitor dispatch via `getattr`** keeps the evaluator flat: add a node class, add a `v_` method.
- **Pascal `DIV` truncates toward zero**, while Python's `//` floors. `-7 DIV 2` is `-3`,
  so I used `int(a / b)`. A test pins this down.
- **Static vs dynamic checks.** Declared types live in a separate table from runtime
  values, so the interpreter can reject `INTEGER := REAL` and coerce `REAL := INTEGER`.

## Known limits

- No procedures, `IF`/`WHILE`, `WRITELN`, or nested scopes (Parts 11+ of the series).
- Type checking happens at run time, not in a separate semantic-analysis pass.
- `int(a / b)` for `DIV` goes through floats, so it's inexact for integers above 2^53.

## Run it

```bash
cd 2026-10-03-python-simple-interpreter
python3 -m unittest -v               # 9 tests pass
python3 interpreter.py example.pas   # prints final variable values
```
