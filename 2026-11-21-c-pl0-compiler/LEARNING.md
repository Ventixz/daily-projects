# Learning: Let's write a compiler (a PL/0 to C compiler, in C)

**Source:** Brian Robert Callahan, "Let's write a compiler", Parts 1–8, from the C/C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The parts are: planning, a lexer, a parser, testing, a code generator, input and output, arrays, and strings
with forward references. briancallahan.net was blocked from the build environment, so this follows the
series' **subject and part list, not its text**. The language extensions below (`readInt`, `writeStr`,
`size` arrays, `forward`) match the part titles, but the exact syntax may differ from the tutorial's.

> Written by the daily-project routine (Claude). Commits are authored as `Claude`, so they are separate
> from the repo owner's own work in the history.

## What it is

`pl0c` compiles **PL/0** to portable C11. PL/0 is the tiny teaching language Niklaus Wirth designed for
*Algorithms + Data Structures = Programs* (1976). It has integers, constants, variables, nested procedures
without parameters, `if`, `while`, and `begin … end`. The compiler is one pass and has no AST: a
recursive-descent parser checks the program and writes C as it reads. Any C compiler turns that into a
native binary. It is about 1,200 lines of C with no dependencies.

```pascal
{ Recursion with a local: each call gets its own copy of 'mine'. }
var n, result;

procedure fact;
var mine;
begin
    mine := n;
    if n <= 1 then result := 1
    else begin n := n - 1; call fact; result := result * mine end
end;

begin n := 5; call fact; writeInt result end.
```

## Run it

```
make                                   # builds ./pl0c (needs a C11 compiler: gcc or clang)
./pl0c tests/programs/squares.pl0 -o squares.c
cc -std=c11 -o squares squares.c && ./squares
./pl0c -t tests/programs/hello.pl0     # print the token stream (line, kind, value)
make test                              # unit tests + end-to-end tests
```

On Windows, use the same commands from an MSYS2 or WSL shell (`winget install MSYS2.MSYS2`, then
`pacman -S gcc make`).

**Tests:** `make test` runs 58 in-process unit checks on the lexer and the generated C, then
**41 end-to-end tests**:

- **18 programs** are compiled to C, built with `-Wall -Wextra -Wpedantic -Werror`, run, and checked against
  the expected stdout, stderr and exit status. They cover recursion, three-level static links, forward
  declarations, arrays, strings, I/O, and runtime errors.
- **23 broken programs** must each be rejected with the exact message given on their first line.

The suite passes with both gcc and clang. It is also clean under AddressSanitizer and UBSan, for the compiler
and for every generated program. 1,500 randomly mutated programs never crashed the compiler.

## The language

```
program    = block "." .
block      = [ "const" ident "=" ["-"] number {"," ...} ";" ]
             [ "var" ident ["size" n] {"," ...} ";" ]
             { "forward" ident ";" | "procedure" ident ";" block ";" }
             statement .
statement  = ident ["[" expr "]"] ":=" expr | "call" ident
           | "begin" statement {";" statement} "end"
           | "if" condition "then" statement ["else" statement]
           | "while" condition "do" statement
           | "readInt" "into" lvalue | "readChar" "into" lvalue
           | "writeInt" expr | "writeChar" expr | "writeStr" ('string' | array)
           | "exit" expr | (empty) .
condition  = "not" c | "odd" expr | expr ("="|"#"|"<"|"<="|">"|">=") expr, joined by "and" / "or" .
expr       = ["+"|"-"] term {("+"|"-") term} .     term = factor {("*"|"/"|"%") factor} .
```

Comments are `{ … }`. `#` means "not equal". Values are 64-bit signed integers.

## Layout

| Path | What it does |
| --- | --- |
| `src/lexer.c` | Hand-written scanner: keywords, `:=`/`<=`/`>=`, `{ }` comments, `'…'` strings with `''` escapes, overflow-checked numbers, line tracking |
| `src/compiler.c` | Parser, symbol table, scope rules, and the C emitter, plus the C runtime that is pasted at the top of every output file |
| `src/main.c` | CLI: `pl0c [-t] [-o out.c] file.pl0` (`-` reads stdin) |
| `tests/unit_tests.c` | Lexer tests and checks on the generated C (static-link paths, escaping, constant inlining) |
| `tests/run_tests.sh` | End-to-end runner: `programs/NAME.pl0` + `.out` [`.in` `.err` `.code`], and `errors/*.pl0` |

## What I learned

- **A one-pass compiler is three small pieces handing tokens forward.** The lexer turns characters into
  tokens and knows nothing about grammar. The parser has **one function per grammar rule**: `block`,
  `statement`, `condition`, `expression`, `term`, `factor`. Each one looks at the current token to decide
  which branch to take. PL/0 was designed so that one token of lookahead is always enough. Code generation
  happens inside those same functions, so there is no tree. This works because PL/0 makes you declare
  everything before you use it, so everything the emitter needs about a name is already in the symbol table.
- **Operator precedence comes from how the rules are layered.** `expression` calls `term`, which calls
  `factor`. That is why `2 + 3 * 4` is 14 without any precedence table. A `{ … }` loop instead of right
  recursion makes `10 - 4 - 3` group to the left (3, not 9). One side effect of Wirth's grammar: a unary minus
  can only start an expression, so `7 % -3` is a syntax error and you have to write `7 % (-3)`. My own test
  file hit that.
- **Expressions can't be streamed out like statements can.** The C for `a - b - c` is
  `pl0_sub(pl0_sub(a, b), c)`. The outer call has to come first in the output, but you only know you need it
  after reading the second `-`. So statements are written straight to the output, while expressions and
  conditions are built up as strings and written when the statement is finished.
- **Nested procedures need a static link when the target has no closures.** An inner PL/0 procedure can
  change variables of the procedure around it, and procedures can recurse. C can't do either directly. Each
  block gets a heap-allocated frame struct holding its variables plus `up`, a pointer to the frame of the
  block around it. A name declared *k* levels out becomes `f->up->…->up->v_name`, and the compiler knows *k*
  from the symbol's depth. A call passes the frame of the block that *declared* the callee: from depth 3,
  calling a procedure declared in `main` passes `f->up->up->up`. Every call gets a fresh frame, so recursive
  calls keep separate locals. `tests/programs/recursion.pl0` checks this. I swapped in one shared `static`
  frame per procedure as a deliberate bug, and the output went from `1 2 3 4 5 120` to `1 1 1 1 1 1`.
- **Scopes are just the symbol table shrinking back.** Entering a block remembers the table's length.
  Leaving it cuts the table back to that length. Lookup searches newest first, so an inner `var x` hides the
  outer `const x` and the outer one is visible again afterwards. Duplicate checks only look at the current
  block's slice. A forward-declared procedure stays as a symbol with `defined = 0`. The later `procedure`
  fills it in, and any still undefined when the block ends is an error.
- **C needs everything declared before use, so emission order matters.** A procedure's prototype is written
  when its header is parsed, so its body can call itself and its parent. Its frame struct comes right after
  its `var` section, before any nested procedures. That way a child's prototype can name its parent's frame
  type. `forward` simply writes the prototype early, which is what makes mutual recursion work.
- **The generated code must not inherit C's undefined behaviour.** In C, signed overflow, division by zero
  and `LLONG_MIN / -1` are undefined. Out-of-bounds indexing silently corrupts memory. So every `+ - * / %`
  and unary minus goes through a checked helper, and every index goes through `pl0_idx`. They stop with
  `runtime error: line N: …`, and the PL/0 source line is compiled into the call. UBSan finds nothing in any
  generated program.
- **Emitting C means escaping for C.** A PL/0 string becomes a C string literal, so `"` and `\` must be
  escaped, and so must `?`. In `-std=c11`, gcc turns `??=` into `#` (a *trigraph*). My own unit test, which
  had `??=` in a C string, was silently changed by gcc before it ever ran and failed. The compiler emits
  `\?`, and non-printable bytes become 3-digit octal escapes, so a following digit can't join the escape.
- **Error messages are part of the design.** The compiler stops at the first error with
  `line N: message`. Each message names what it found ("expected 'then' but found identifier 'x'") and the
  earlier declaration line for duplicates. An unterminated comment is reported at the line where it
  *started*, not at end of file. A missing final `.` is reported on the last line with code, not on the
  blank line after it.
- **Hostile input must not crash the compiler.** Recursive descent recurses on nesting, so 5,000 `(` or
  `not not not …` could overflow the C stack. A depth counter turns that into "nested too deeply". The
  setjmp/longjmp error exit keeps the compiler state on the heap: after a `longjmp`, local variables that
  changed since `setjmp` have indeterminate values, but memory reached through an unchanged pointer is fine.

## Things to try next

- Procedures with parameters and return values (`function f(a, b)`). That means passing arguments as extra C
  parameters next to the static link.
- A second backend that emits x86-64 assembly or a small stack VM instead of C, keeping the same parser.
- Constant folding: `2 * 3 + k` can be computed at compile time when every operand is a constant.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Based on the subject of "Let's write a compiler" by Brian Robert Callahan (https://briancallahan.net/blog/20210814.html).
