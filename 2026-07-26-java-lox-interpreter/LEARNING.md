# Interpreter (Java)

**Source:** [Build an Interpreter](http://www.craftinginterpreters.com/) (chapters 4-10, jlox),
from [practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

## What it is

A tree-walking interpreter for a subset of Lox: numbers, strings, booleans, `nil`, arithmetic,
comparisons, `and`/`or`, variables, blocks with lexical scoping, `if`/`while`/`for`, and functions
with closures. `Scanner` turns source text into a flat token list, `Parser` is hand-written
recursive descent that turns tokens into an `Expr`/`Stmt` AST, and `Interpreter` walks that AST
directly with the visitor pattern — no bytecode, no compilation pass. `Environment` is a chain of
hash-map scopes that gives variables their lexical lifetime and is the whole mechanism behind
closures. Classes and inheritance (jlox chapters 11-13) are deliberately out of scope for a single
day.

## Run it

```bash
cd 2026-07-26-java-lox-interpreter
make test                          # 43 tests, no dependencies to install (no JUnit, no Maven)
make run SCRIPT=examples/fibonacci.lox
make run SCRIPT=examples/counters.lox
make repl                          # line-at-a-time REPL
```

## What it actually teaches

- **Precedence is encoded in the call graph, not a table.** The parser has one method per
  precedence level (`equality` → `comparison` → `term` → `factor` → `unary` → `call` → `primary`),
  and each one only calls *tighter*-binding methods for its operands. `2 + 3 * 4` parses correctly
  not because of any explicit precedence number, but because `term()` (handles `+`/`-`) calls
  `factor()` (handles `*`/`/`) for each operand, so `3 * 4` is fully consumed as a single unit
  before `term` ever sees the `+`. Changing precedence means moving a method one level up or down
  the chain, not editing a table and hoping every caller agrees with it.
- **A closure is just a function value plus the environment that was live when it was declared.**
  `LoxFunction` stores a reference to `closure`, the `Environment` in scope at the `fun` statement,
  not at the call site. Every call then creates a *new* environment chained off that saved one
  (`new Environment(closure)`), so two different calls to the same function never share locals,
  but a function returned from another function still sees that enclosing function's variables
  forever. `examples/counters.lox` makes this concrete: `makeCounter()` called twice produces two
  `increment` closures with entirely separate `count` cells, because each call to `makeCounter`
  allocates its own fresh `Environment`.
- **Statements return nothing; expressions return a value — that split is a type signature, not a
  convention.** `Stmt.Visitor<Void>` and `Expr.Visitor<Object>` are different generic
  instantiations of the same visitor pattern. The compiler enforces that a `print` statement can
  never be used where a value is expected, and an expression like `1 + 2` can never accidentally
  be treated as a control-flow statement — there's no runtime check for it, the two visitor
  interfaces just don't share a supertype.
- **`return` unwinds the Java call stack by throwing, not by checking a flag after every
  statement.** `visitReturnStmt` throws a `Return` (carrying the value), and `LoxFunction.call`
  catches it around the whole function body. That means `return` works correctly no matter how
  deeply it's nested inside `if`/`while`/blocks inside the function — every intervening
  `executeBlock`/`execute` call just lets the exception fall through unnoticed, instead of every
  statement-executing method needing to check "did a nested statement already return?" and
  propagate that manually. `Return` disables Java's stack-trace capture in its constructor
  (`super(null, null, false, false)`) since it's control flow, not an error, and building a trace
  for every function return would be pure waste.
- **`for` doesn't exist to the interpreter — the parser desugars it into `while`.** There's no
  `Stmt.For` or `visitForStmt`. `Parser.forStatement()` builds `initializer; while (condition) {
  body; increment; }` out of existing `Stmt.Block`/`Stmt.While`/`Stmt.Expression` nodes and hands
  that to the interpreter. One evaluation rule (`while`) now covers two syntaxes, and the `for`
  loop automatically gets whatever `while` already does correctly (like `return` unwinding out of
  it) for free.

## What I'd add next (stretch goals I skipped for scope)

- A resolver pass (jlox ch. 11) that binds each variable reference to a scope distance at parse
  time, so `Environment.get`/`assign` stop walking the chain at runtime — this is also what fixes
  the classic bug where a closure over a variable later shadowed in an enclosing block resolves
  inconsistently.
- Classes, methods, and single inheritance (ch. 12-13), which is most of what turns this from an
  expression evaluator into an actual small language.
- Better runtime errors — right now `1 - "nope"` reports "Operands must be numbers" with a line
  number but no indication of the containing statement, which gets harder to use once functions
  are calling functions several frames deep.
