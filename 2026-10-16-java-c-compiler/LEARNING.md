# Learning: A Tiny C Compiler, in Java

**Source:** ["Write a C Compiler"](https://norasandler.com/2017/11/29/Write-a-Compiler.html) (Nora Sandler),
from the C/C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning),
reimplemented in Java (standard library only). Covers roughly parts 1-5: integers, unary and
binary operators, and local variables.

Build and test: `cd src && javac -d ../out Compiler.java TestRunner.java && java -cp ../out TestRunner`
(needs `gcc` to assemble/link). Compile a file: `java -cp out Compiler tests/hello.c > hello.s && gcc -no-pie hello.s -o hello && ./hello; echo $?`

## What it is

- `src/Compiler.java`: lexer, recursive-descent parser that emits x86-64 AT&T assembly directly
  (no AST), for a single `int main()` with `return`, `int` locals, assignment, and the C operators
  `- ~ ! + - * / % < > <= >= == != && ||`.
- `src/TestRunner.java`: compiles 21 programs, assembles each with gcc, runs it and checks the exit
  code; also checks that 4 invalid programs are rejected.

## What I learned

- **Operator precedence is just grammar layering.** One method per precedence level
  (`||` > `&&` > equality > relational > additive > multiplicative > unary) gives the right
  associativity for free; `10-3-2` is 5 because each level loops rather than recurses on the right.
- **A stack machine is the simplest correct codegen.** Every expression leaves its result in
  `%eax`; for a binary op, push the left, evaluate the right, move it to `%ecx`, pop the left. Slow,
  but there is no register allocation to get wrong.
- **`&&` and `||` must short-circuit.** They need labels and jumps, not an instruction; the tests
  `0 && (a=5)` and `1 || (a=5)` prove the right side is skipped.
- **Division needs sign extension.** `idivl` divides `%edx:%eax`, so `cltd` must run first, and the
  remainder lands in `%edx`. `-7/2` truncating toward zero matches C.
- **Comparisons:** `cmpl` + `setX %al` after zeroing `%eax` (zero first, since `cmp` clobbers flags that
  `xor`/`mov $0` would not... `movl` doesn't touch flags, which is why it's used rather than `xorl`).
- **Locals live at fixed offsets from `%rbp`.** Pushing each declaration's value keeps `%rsp` in sync
  with the offset table, so no separate frame-size pass is needed. A flat map is enough until blocks
  introduce nested scopes.
- **Testing a compiler end to end is cheap:** the exit code of the produced binary is the oracle.

## Known limits

- One function, no calls, no `if`/loops, no blocks/scopes, no `int` literal range check.
- Exit codes are 0-255, so tests keep results small. Assignment only to plain identifiers.
- Linux x86-64 only (relies on the System V ABI and `gcc` for assembling).
