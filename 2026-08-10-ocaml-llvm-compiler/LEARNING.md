# Implementing a Language with LLVM (OCaml)

**Source:** ["Implement a Language with LLVM in OCaml"](https://llvm.org/docs/tutorial/#kaleidoscope-implementing-a-language-with-llvm-in-objective-caml),
the OCaml entry of LLVM's own Kaleidoscope tutorial series, listed under
OCaml in
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The usual approach binds against `llvm`/`llvm.irbuilder` via opam. Those
bindings weren't available in this environment (no `opam`, and installing
one would mean pulling a large package tree over a proxy I didn't want to
lean on), so I took the constraint at face value: emit LLVM IR as *text*,
directly, by hand. `clang`/`llc`/`lli` were already on the machine, so the
back half of the pipeline -- turning `.ll` into a real native binary -- is
the genuine, unmodified LLVM toolchain. Only the IR-generation side is
hand-rolled.

## What it is

A compiler for **Kal**, a small expression-oriented language: `def`/`extern`
top-level declarations, integer arithmetic, `if`/`then`/`else`, `let` (with
comma-chained bindings), a `for` loop, and function calls including
recursion and mutual recursion. Every value is `i32` -- no floats, no
strings, no arrays -- which keeps the type story out of the way of the part
actually being practiced: control-flow codegen.

- `src/lexer.ml` -- hand-cursor tokenizer, no regex library.
- `src/parser.ml` -- recursive descent, with a precedence-climbing
  `parse_binop_rhs` for `* / + - < > ==` (the same structure the original
  Kaleidoscope C++ tutorial uses for its `ParseBinOpRHS`).
- `src/codegen.ml` -- walks the AST and emits LLVM IR text. This is the
  interesting file; see below.
- `src/main.ml` -- `kalc file.kal` prints IR to stdout.
- `runtime.c` -- one function, `print_int`, so Kal programs can produce
  visible output; declared on the Kal side with `extern print_int(x);`
  and linked in as a native object.
- `tests/test_kalc.ml` -- 15 end-to-end cases. Each one compiles Kal source
  to IR *in-process*, then genuinely shells out to `clang` to assemble and
  link it, runs the resulting binary, and checks its stdout. A test only
  passes if LLVM itself accepts the IR -- not just if the OCaml code didn't
  raise.

## Run it

```bash
cd 2026-08-10-ocaml-llvm-compiler
make test            # builds run_tests, links against runtime.o, runs 15 cases
make examples         # compiles examples/*.kal to native binaries and runs them
./kalc examples/fib.kal    # prints raw LLVM IR to stdout
```

No opam packages, no dune -- `make build` is a single `ocamlopt` invocation
over four files plus `main.ml`.

## What it actually teaches

- **Textual IR can't be edited after the fact, and a loop's phi node needs
  a value that doesn't exist yet.** A `for` loop's induction variable is a
  `phi i32 [ start, %preheader ], [ next_i, %latch ]` -- but the phi has to
  be the first instruction in the loop header, and `next_i` (the
  incremented value) is only computed *after* the loop body runs, in a
  later block. The real `llvm::IRBuilder` sidesteps this because a `PHINode`
  is a mutable object -- you create it with placeholder operands and call
  `addIncoming` once you know the answer. Text has no such handle. I ended
  up writing `Codegen.Lines`, a tiny growable array with a `reserve`/`set`
  pair (`codegen.ml`): grab a blank line's index before you know its
  contents, fill it in later. `gen_expr`'s `For` case reserves the phi's
  line right after entering the loop header, generates the whole body
  (which can itself open and close arbitrarily many blocks), and only then
  goes back and writes the real phi instruction into the reserved slot.
  Needing to build that at all -- rather than just calling an API -- is the
  concrete cost of not having bindings, and it's also *why* the
  IRBuilder's mutable-handle API exists in the first place.
- **The block you jump into isn't necessarily the block you're still in
  when you jump back out.** My first draft of `if`-codegen wired the merge
  block's `phi` predecessors to `then_lbl`/`else_lbl` -- the labels the
  branch targets. That's wrong the moment either arm contains its own
  control flow: `classify(-20)` in `examples/`-style code nests an `if`
  inside a `then`-branch, so by the time that inner `if` finishes, code
  generation has moved on to *its own* `ifcont` block -- three blocks
  removed from `then_lbl`. `gen_expr`'s `If` case now captures
  `st.current_block` immediately after generating each arm
  (`then_end`/`else_end` in `codegen.ml`) and uses *that* as the phi
  predecessor, not the label the branch jumped to. The
  `"nested if in a then-branch"` test in `test_kalc.ml` pins exactly this:
  it fails silently (LLVM accepts a phi with a predecessor that just isn't
  reachable that way -- it doesn't verify *which* block dynamically reaches
  the phi) unless you actually check the printed values, which is why the
  test asserts concrete output (`100`, `200`, `5`) rather than just "clang
  didn't error." The `for` loop has the identical bug shape for its
  latch block, caught by a matching "`nested if inside a for body`" test.
- **Every value being `i32` still means two implicit type conversions,
  not zero.** `icmp` produces `i1`; every other Kal value is `i32`; `br`
  needs `i1`. So a comparison (`gen_comparison` in `codegen.ml`) computes
  `icmp slt/sgt/eq` and immediately `zext`s the `i1` result back to `i32` --
  otherwise `3 < 5` couldn't be added, printed, or stored like any other
  value. Symmetrically, `if`'s condition takes an arbitrary `i32` and
  narrows it with `icmp ne i32 %cond, 0` to get the `i1` a `br` requires.
  Collapsing "everything is i32" into "therefore no conversions" was my
  first assumption, and it's wrong at exactly these two crossings.
- **Global forward references just work in LLVM IR; the compiler doesn't
  need a symbol table pass.** `is_even` and `is_odd` call each other before
  either's `define` appears complete in the emitted module, and no explicit
  forward `declare` is emitted for either -- `compile` in `codegen.ml`
  writes `declare` only for `extern`s, then all the `define`s in source
  order. It works because LLVM's assembler resolves a module's global
  symbols after parsing the whole file, the same way object-file symbol
  resolution doesn't care whether a `call` textually precedes a function's
  definition. I didn't build that; I just didn't need to build a
  hand-rolled prototype pass, which is the kind of thing you can only be
  sure of by testing it -- hence `"mutual recursion, forward reference
  resolves"` actually running two functions that reference each other
  before either is complete, not just asserting it compiles.
- **A language with no modulo operator still has one, syntactically --
  it's just not spelled `%`.** `examples/primes.kal` needs `n mod d` and
  Kal has no such operator (deliberately -- keeping the operator set to
  `+ - * / < > ==` was a scope decision). `n - (n / d) * d` gets there using
  only integer division's truncation. This isn't a Kal-specific trick, it's
  the actual definition of modulo in terms of truncating division; writing
  it out by hand once, instead of reaching for an operator, is what made
  that visible.

## Deliberate scope cuts

- **No mutable variables / no `alloca`.** The original Kaleidoscope
  tutorial has a whole chapter (7) on switching from pure-SSA `let` to
  `alloca` + `load`/`store` + the `mem2reg` pass, specifically so
  *user-visible* mutation (`x = x + 1` as a statement, not a new binding)
  is possible. Kal's `for` loop gets its counter for free via the phi
  technique above without needing that; adding real mutable variables would
  reuse the same `alloca` chapter almost verbatim.
- **No floats.** Everything is `i32`. Swapping to `double` mainly touches
  `gen_comparison` (`fcmp` instead of `icmp`, no `zext` needed since Kal
  would then need actual booleans) and number parsing in the lexer.
- **`end`/`step` in a `for` are evaluated once, not per iteration.** The
  original tutorial re-evaluates them every loop iteration, which matters
  if they have side effects (a call). I evaluate `start`, `end`, and `step`
  once in the preheader block for simpler, more predictable semantics --
  a scope decision, not an oversight; documented here since it's the kind
  of thing that's easy to get "for free" the other way and not notice
  which one you built.
