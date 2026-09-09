# Learning: Writing a basic x86-64 JIT compiler from scratch in stock Python

**Source:** ["Writing a basic x86-64 JIT compiler from scratch in stock Python"](https://csl.name/post/python-jit/)
by Christian Stigen Larsen, from the Miscellaneous subsection of the Python
section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting, so this folder contains the
finished implementation directly at the project root (no separate
`reference/`). Zero external dependencies — standard library only
(`ctypes`, `mmap`, `dataclasses`).

## What it is

A tiny arithmetic-expression compiler that emits real x86-64 machine code
and executes it, instead of interpreting an AST:

- `src/lexer.py` / `src/parser.py` — a standard recursive-descent parser
  for `+ - * /`, parentheses, unary minus, and single-lowercase-letter
  variables (`a`..`z`), producing the `Num`/`Var`/`BinOp`/`Neg` nodes in
  `src/ast_nodes.py`.
- `src/codegen.py` — the actual point of the project. `emit()` walks the
  AST and writes out hand-encoded x86-64 opcode bytes (REX prefixes,
  ModRM bytes, immediates) for a stack machine: every node's code leaves
  one more value pushed on the hardware stack than before it ran, so
  combining two subtrees is always "generate left, generate right, pop
  both into `rax`/`rbx`, combine, push the result." No assembler and no
  register allocator — `rax`/`rbx` are just reused as scratch on every
  operation.
- `src/jit.py` — `JitFunction` allocates a page with `mmap.PROT_EXEC`,
  copies the machine code in, and wraps its address in a `ctypes`
  function pointer with the System V AMD64 calling convention (`rdi`
  holds a pointer to a 26-slot `int64_t` array, one slot per variable
  letter; the result comes back in `rax`).
- `src/interpreter.py` — a plain tree-walking evaluator over the same
  AST, used only as the correctness oracle the JIT is checked against.
- `demo.py` — compiles an expression, runs it both ways, and prints the
  generated machine code as a hex dump.
- `tests/` — 34 tests: lexer, parser, interpreter, direct JIT checks, and
  a 50-case fuzz test that generates random expressions and asserts the
  JIT and the interpreter agree on every one.

## Run it

```bash
cd 2026-09-09-python-x86-jit
python3 -m unittest discover -v      # 34 tests
python3 demo.py "3 + 4 * (2 - 5)"
python3 demo.py "a * a + b" a=6 b=1
python3 demo.py                      # interactive REPL
```

Actual output:

```
$ python3 demo.py "3 + 4 * (2 - 5)"
expr:      3 + 4 * (2 - 5)
jit:       -9
interp:    -9
match:     True
machine code (65 bytes): 48 b8 03 00 00 00 00 00 00 00 50 48 b8 04 00 00 ...

$ python3 demo.py "a * a + b" a=6 b=1
expr:      a * a + b
variables: {'a': 6, 'b': 1}
jit:       37
interp:    37
match:     True
machine code (39 bytes): 48 8b 87 00 00 00 00 50 48 8b 87 00 00 00 00 50 ...
```

## What it actually teaches

- **A "JIT" here just means: write bytes into memory, then jump the CPU
  into them.** There's no bytecode format, no interpreter loop underneath
  — `JitFunction.__init__` marks an `mmap` page executable and hands its
  raw address to `ctypes.CFUNCTYPE`, and calling the resulting Python
  object *is* a `call` instruction landing directly on hand-written
  opcode bytes. `test_reused_jit_function_is_pure` calling the same
  compiled buffer twice with different variable bindings is the proof
  that this is genuinely compiled code running (state lives in the
  caller-supplied array, not anywhere baked into the bytes), not an
  elaborate way of re-interpreting the AST every time.
- **A stack machine needs no register allocator, at the cost of
  redundant loads/stores real compilers would eliminate.** Every `emit()`
  call for a subtree ends by pushing its one result value; every binary
  op starts by popping its two operands back out. `a * a + b` in the demo
  above loads `a` from memory *twice* (`8b 87 00000000` appears twice)
  because the codegen has no notion of "this value is still in a
  register from last time" — each `Var` node is compiled in isolation.
  That's the real tradeoff a stack-based codegen makes for simplicity.
- **Operand order survives the push/pop round-trip only if you get the
  pop order right.** For `left - right`, `emit(BinOp)` generates `left`
  then `right`, so `right` (pushed last) is on top of the stack. `pop
  rbx` has to run before `pop rax` so that `rax` ends up holding `left`
  and `sub rax, rbx` computes `left - right`, not `right - left`.
  `test_addition_subtraction_multiplication` (`3 + 4 * (2 - 5)` ==
  `-9`, not `9`) is exactly the test that fails first if that pop order
  is swapped.
- **Division needs one extra instruction the other three operators
  don't: `cqo`.** `idiv` divides the 128-bit value in `rdx:rax` by its
  operand, not just the 64 bits in `rax` — skip sign-extending `rax` into
  `rdx` first and a negative dividend silently divides using whatever
  garbage was already sitting in `rdx`. `src/interpreter.py`'s
  `truncating_div` exists specifically because Python's own `//` floors
  toward negative infinity while `idiv` truncates toward zero;
  `test_truncating_division_matches_interpreter` (`-7 / 2 == -3`, not
  `-4`) is the case that tells the two apart.
- **Immediates and memory operands are wrapped/sign-extended, not
  type-checked, by the hardware.** `mov rax, imm64` writes whatever
  8-byte pattern `to_bytes(8, "little", signed=False)` produces —
  `test_negative_literal_round_trips` checks that a negative Python int
  survives being written as its unsigned two's-complement bit pattern
  and coming back out negative through `ctypes.c_int64`'s return type,
  which is where the sign actually gets reinterpreted, not in the
  machine code itself.

## Deliberate scope cuts

- **No comparison or boolean operators, and no control flow at all.**
  Every generated function is a straight-line sequence of pushes, pops,
  and arithmetic with a single `ret` at the end — there's no `Jcc`
  encoding, so nothing here needed to solve jump target resolution
  (forward references, relative offsets) at all. The Go blockchain project
  two days before this one and the C compiler project a week before it
  are the ones in this repo that actually have to solve that.
- **Division by zero is a real SIGFPE, and this project doesn't stop
  it.** `idiv` by zero is a hardware fault; unlike a normal Python
  exception, nothing in this process can catch it, so hitting it kills
  the interpreter outright instead of raising `ZeroDivisionError`. Adding
  a guard would mean emitting a conditional test-and-jump before every
  `idiv`, which is exactly the control-flow machinery this project
  intentionally left out (see above). `tests/test_jit.py`'s fuzzer
  works around this by construction — every generated divisor is shifted
  into `[1, 101]` before it's ever handed to the JIT — rather than
  proving the JIT survives it, because it can't.
- **26 fixed variable slots, always disp32-encoded.** Every variable load
  uses the 7-byte `mov rax, [rdi + disp32]` form, even for `a` at offset
  0, where a real assembler would use the shorter disp8 or no-displacement
  encoding. One encoder path instead of three, at the cost of a few
  redundant zero bytes per load.
- **No constant folding, no peephole optimization, no common
  subexpression elimination.** `a * a + b` loads `a` twice, as noted
  above; the codegen makes no attempt to notice repeated subtrees or
  fold `2 - 5` into a single immediate at compile time.

## What I'd add next

- **Comparison operators plus `Jcc` encoding**, to get from "arithmetic
  calculator" to something that can compile an `if`. This is the natural
  next step and the one every scope cut above is downstream of.
- **A register allocator for a handful of variables**, so `a * a + b`
  loads `a` once instead of twice — the kind of thing that turns a
  correctness demo into something worth calling a compiler backend.
- **A software guard against division by zero** — check the divisor
  against zero and raise a clean Python exception before ever reaching
  `idiv`, once `Jcc` support exists to make the branch possible.
