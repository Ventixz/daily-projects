# Learning: Write a C Compiler (C++)

**Source:** ["Write a C Compiler"](https://norasandler.com/2017/11/29/Write-a-Compiler.html) by
Nora Sandler, from the C/C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Picked and built end-to-end in one sitting, so this folder contains the finished implementation
directly at the project root (no separate `reference/`).

## What it is

`mcc`, a compiler for a real (if small) subset of C, written in C++, that emits x86-64 assembly
directly — no LLVM, no libgccjit, just a lexer, a recursive-descent parser, and a codegen pass
that turns an AST into text an assembler understands:

- `src/lexer.{h,cpp}` — hand-written tokenizer: keywords, identifiers, int literals, the two-vs-
  one-character operators (`==` must not be seen as two `=`s), `//` and `/* */` comments. Throws
  on the first bad character with a line number, no error recovery.
- `src/ast.h` — the node types: expressions carry a `Kind` tag and a `slotOffset` field the
  resolver fills in later; statements cover the usual set (`if`/`while`/`for`/`return`/`break`/
  `continue`/blocks/declarations).
- `src/parser.{h,cpp}` — one recursive-descent function per precedence level (assignment → `||` →
  `&&` → `|` → `^` → `&` → equality → relational → additive → multiplicative → unary → primary),
  so `1 + 2 * 3` and `1 + 2 < 3` parse with the right shape without a precedence table.
- `src/resolver.{h,cpp}` — a semantic pass that runs *before* codegen: collects every function's
  name and arity up front (so forward references and mutual recursion work regardless of source
  order), walks each function body with a stack of scopes to assign every local/parameter a fixed
  `rbp`-relative stack slot, and rejects undeclared variables, same-scope redeclarations,
  `break`/`continue` outside a loop, and calls with the wrong argument count — all before a single
  line of assembly is emitted.
- `src/codegen.{h,cpp}` — a one-pass stack-machine emitter: every expression leaves its result in
  `eax`; binary operators evaluate both sides through `push`/`pop` around a shared stack rather
  than a register allocator. Intel-syntax output (`.intel_syntax noprefix`), so `mov eax, ecx`
  reads left-to-right as "eax = ecx".
- `runtime/runtime.c` — the one thing mini-C programs can call to produce output, `print_int`,
  written in real C so it links identically into both an mcc-compiled binary and a gcc-compiled
  one (see testing, below).
- `tests/unit/` — 76 hand-rolled assertions across the lexer (tokenizing, comments, line tracking,
  error cases) and the parser/resolver (operator precedence shape, scoping, forward-declaration
  resolution, every rejection case).
- `tests/programs/` + `tests/run_tests.py` — 15 example programs, each compiled two independent
  ways and diffed (see below).

## Run it

```bash
cd 2026-09-03-cpp-mini-c-compiler
make test     # 76 unit assertions + 15 differential end-to-end programs + 8 rejection cases
make run      # builds examples/fib.c and runs it
```

Actual session:

```
$ make run
./build/mcc examples/fib.c -o build/fib.s
gcc build/fib.s runtime/runtime.c -o build/fib
./build/fib
0
1
1
2
3
5
8
13
21
34
```

`examples/fib.c` is 13 lines of ordinary-looking C — a recursive `fib`, a `for` loop calling it,
`extern void print_int(int x);` for output. `mcc` turns it into real x86-64, `gcc` assembles and
links that assembly with the runtime, and the resulting native binary runs standalone with no
interpreter in sight. A slice of what `fib`'s body compiles to (`.intel_syntax noprefix`, so
`mov eax, ecx` means "eax = ecx"):

```asm
fib:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov [rbp-8], edi        ; the incoming register argument becomes a normal stack slot
    mov eax, [rbp-8]
    push rax
    mov eax, 2
    pop rcx
    cmp ecx, eax             ; ecx=n, eax=2 -> flags for "n < 2"
    setl al
    movzx eax, al
    cmp eax, 0
    je .Lelse0
    mov eax, [rbp-8]
    leave
    ret
.Lelse0:
    ...
    call fib                 ; fib(n-1), no padding needed here
    push rax
    ...
    pop rdi
    sub rsp, 8               ; the *second* call in this expression needs 8 bytes of padding
    call fib                 ; fib(n-2)
    add rsp, 8
    pop rcx
    add eax, ecx
    leave
    ret
```

## What it actually teaches

- **A stack machine turns "what register is this in?" into a non-question.** Every operator
  evaluates its left operand, `push`es it, evaluates its right operand into `eax`, then `pop`s the
  left back into `ecx`. No register allocator, no liveness analysis, no spilling logic — the x86
  stack itself *is* the spill space, always. It's wasteful (a real compiler would keep `n` in a
  register across the two `fib` calls instead of reloading it from `[rbp-8]` every time) but it's
  the reason this compiler has zero register-allocation bugs: there's no allocation to get wrong.
- **SysV stack alignment is a per-call-site fact, not a per-function one, once temporaries are
  involved.** `rsp` is 16-aligned right after the prologue (`push rbp` undoes the 8 bytes `call`
  pushed, and `sub rsp, FRAME` uses a 16-byte-rounded `FRAME`), but every `push` of an intermediate
  value shifts it by 8. `fib(n-1) + fib(n-2)` makes that concrete: the first `call fib` happens
  with zero outstanding pushes (aligned, no padding needed); the second happens with the first
  call's *result* still sitting on the stack via `push rax` (one outstanding push, misaligned by
  8), so `codegen.cpp`'s `tempDepth_` counter — incremented and decremented in lockstep with every
  `push`/`pop` the whole codegen ever emits, argument-passing included — is what decides whether
  that `sub rsp, 8` / `add rsp, 8` pair needs to exist. Get this wrong and the symptom isn't a
  wrong answer, it's a `movaps`-based SIGSEGV three call frames later inside glibc's `printf`,
  which is a brutal thing to debug from a wrong-answer test alone — the differential test suite
  running everything as real, executed binaries is what caught it during development, not a
  read of the assembly.
- **Resolving names and generating code are genuinely separate jobs, and doing them as two passes
  removes a whole class of ordering bugs.** `is_odd` calling `is_even` before `is_even` is even
  parsed only works because `resolver.cpp`'s `collectSignatures` scans every function's name and
  arity *before* any function body is walked — codegen never has to care what order things appear
  in the source, because by the time it runs, every `CallExpr` and `VarRefExpr` already carries
  everything it needs.
- **An initializer has to resolve in the scope *before* its own name exists.** `resolveStmt` for a
  `VarDeclStmt` calls `resolveExpr(init)` and only afterwards calls `declare(name)` — so
  `int x = x;` resolves the right-hand `x` against whatever `x` was in an outer scope (or fails if
  there isn't one), matching real C, and `test_shadowing_allowed_redeclaration_in_same_scope_rejected`
  plus the `scoping_and_shadowing.c` end-to-end test both exercise the shadowing side of the same
  mechanism: a fresh scope pushed per block, discarded on exit, means an inner `int x` never
  touches the outer one's stack slot.
- **Differential testing against a trusted compiler is a much stronger correctness net than
  hand-written expected-output files, for zero extra maintenance.** Every `tests/programs/*.c`
  file is ordinary standard C. `tests/run_tests.py` compiles each one with `mcc` *and* with `gcc`
  directly, links both against the same `runtime.c`, runs both, and diffs stdout and exit code.
  There is no hand-typed "expected: 55" anywhere to get subtly wrong or forget to update — gcc
  *is* the oracle, for the entire subset this compiler claims to support (`division_and_modulo.c`
  is the case that would have been easy to get wrong and hard to notice: C truncates division
  toward zero and remainder follows the dividend's sign, `-17 / 5 == -3` and `-17 % 5 == -2`, not
  the mathematician's floor-division answer — the test doesn't assert a specific number, it just
  asserts mcc agrees with gcc).

## Deliberate scope cuts

- **One type: 32-bit `int`.** No `long`, `double`, pointers, arrays, or `struct`s. This is also
  what makes differential testing against gcc so clean — there's no type-conversion corner where
  the two compilers could disagree for reasons unrelated to a real bug.
- **No global variables, no `static`.** Every identifier is a local or a parameter; the resolver's
  scope stack never needs a "module scope" case.
- **At most 6 parameters/arguments.** SysV passes the first 6 integer arguments in registers
  (`rdi`/`rsi`/`rdx`/`rcx`/`r8`/`r9`) and the rest on the stack; this compiler only implements the
  register path (`tests/invalid/too_many_parameters.c` checks the 7th-parameter case is rejected
  with a clear error rather than silently miscompiled).
- **No register allocation.** Every intermediate value round-trips through the stack via
  `push`/`pop`, and every local variable lives in a fixed stack slot for the whole function, even
  one used inside a tight loop. Correct, not fast.
- **Stack slots are never reused across sibling scopes.** `{ int a; }` followed by `{ int b; }`
  gets two different offsets even though their live ranges never overlap — simpler to resolve
  (every declaration just claims the next slot) at the cost of a slightly larger frame than
  necessary.
- **Locals without an initializer are zero-filled.** Real C leaves them undefined; this compiler
  always emits `mov eax, 0` first so behavior stays deterministic instead of depending on stack
  garbage from a previous call frame.

## What I'd add next

- **A `long`/`double` type (or even just a second integer width)** would force the codegen's
  "everything is one 32-bit value in `eax`" assumption to become an actual type system, and is the
  natural next step before anything else on this list.
- **Stack-slot reuse via live-range analysis**, so two variables that are never live at the same
  time can share an offset — the frame-size computation in `resolver.cpp` already visits every
  declaration in order, so this is a change to what it does with that information, not to the
  traversal itself.
- **Spilling only what's actually needed**, i.e. a real (even a trivial linear-scan) register
  allocator, replacing the push/pop-everything stack machine — the biggest legibility-for-speed
  trade this version makes.
- **Arguments 7 and beyond, passed on the stack** per the SysV ABI, removing the current hard cap.
