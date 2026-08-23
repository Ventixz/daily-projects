# Build Your Own Lisp (C)

**Source:** [Build Your Own Lisp](https://www.buildyourownlisp.com/) by Daniel
Holden, from the C section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The book has you vendor its own `mpc` parser-combinator library for lexing and
parsing; this repo's convention is zero external dependencies (see
`2026-08-22-go-bittorrent-client/bencode/bencode.go`'s hand-rolled bencode
codec), so the lexer and recursive-descent parser here are written from
scratch against nothing but the C99 standard library.

## What it is

- `src/lval.c` / `lval.h` -- the tagged-union `Lval` value type (Number,
  Error, Symbol, String, Sexpr, Qexpr, Function) plus its full lifecycle:
  constructors, `lval_copy` (deep copy, including function closures),
  `lval_del`, list manipulation (`lval_add`/`pop`/`take`/`join`), and
  printing. Numbers are stored as `double` throughout rather than `long`,
  so `/` and mixed arithmetic never need a second numeric type; `%` and
  the printer both special-case the common case of "this double happens to
  be an integer" rather than switching representations.
- `src/lenv.h` / `lenv.c` -- the `Lenv` environment: parent-chain symbol
  lookup for lexical scoping, plus manual reference counting
  (`lenv_retain`/`lenv_release`) so a closure can safely keep a defining
  environment alive after the scope that created it returns. The root
  environment is deliberately exempt from refcounting and freed once,
  explicitly, via `lenv_free_root` -- see the closure-cycle lesson below.
- `src/parser.h` / `parser.c` -- a hand-written lexer and recursive-descent
  parser: integers/doubles, symbols, strings (with `\n \t \r \\ \"`
  escapes), `;`-to-end-of-line comments, `(...)` S-expressions, `{...}`
  Q-expressions. A syntax error anywhere (unmatched bracket, unterminated
  string, stray closing bracket, trailing garbage) is converted into a
  single `LVAL_ERR` value rather than a separate error channel, so the
  REPL loop never needs a special case for "parsing failed."
- `src/builtins.c` / `builtins.h` -- `lval_eval`/`lval_eval_sexpr` (the
  evaluator), `lval_call` (function application, including currying), and
  every builtin: arithmetic `+ - * / % ^`, comparisons
  `> < >= <= == !=`, boolean `&& || !`, `if`, list ops
  `list head tail join eval cons len init`, `def` (global) and `=`/`put`
  (local) variable definition, `\` (lambda) and `fun` (named-function
  sugar), `do` (sequence, returns its last value), `print`, `error`.
- `src/main.c` -- the REPL: with no argument it reads lines from stdin and
  prints one result per top-level form; with a file argument it reads the
  whole file and evaluates every top-level form in it, so a script can be
  run and checked non-interactively with no TTY involved.
- `resources/recursion.lisp` -- `factorial`, `fib`, and an
  accumulator-style `sum-to`, each defined with `fun` and calling itself
  by name -- ordinary user-level recursion, not a C builtin.
- `resources/closures.lisp` -- `make-adder` returning a lambda that closes
  over its own `n`; a `make-counter` example that demonstrates (and
  explains) what closing over a variable does and does *not* buy you
  here; and manual currying of a 3-argument lambda one argument at a time.
- `tests/test_lisp.c` -- 18 hand-rolled assert-based tests covering parser
  edge cases (empty S-expression, nested braces, trailing garbage,
  unmatched brackets, multi-form input, strings/comments), arithmetic
  error values (division/modulo by zero, wrong arg count/type), list
  operations, the closure/lexical-scoping regression, local `=` not
  leaking into the global environment, currying, and two real recursive
  functions (factorial, fibonacci) evaluated to exact expected numbers.

## Run it

```bash
cd 2026-08-23-c-lisp
make all    # builds ./lisp
make test   # builds and runs ./run_tests
```

Actual `make test` output:

```
gcc -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g -o run_tests tests/test_lisp.c src/lval.c src/lenv.c src/parser.c src/builtins.c -lm
./run_tests
PASS test_parse_empty_sexpr
PASS test_parse_nested_braces
PASS test_parse_trailing_garbage_is_error
PASS test_parse_unmatched_open_paren_is_error
PASS test_parse_multiple_top_level_forms
PASS test_parse_string_and_comment
PASS test_arithmetic_basic
PASS test_division_by_zero_is_error_value
PASS test_modulo_by_zero_is_error_value
PASS test_wrong_arg_count_is_error_value
PASS test_wrong_arg_type_is_error_value
PASS test_list_operations
PASS test_closure_captures_definition_env
PASS test_local_put_does_not_leak_to_global
PASS test_currying_partial_application
PASS test_recursive_factorial
PASS test_recursive_fibonacci
PASS test_unbound_symbol_is_error_value
All tests passed.
```

Actual `valgrind --leak-check=full --error-exitcode=1 ./run_tests` summary:

```
==4645== HEAP SUMMARY:
==4645==     in use at exit: 0 bytes in 0 blocks
==4645==   total heap usage: 222,092 allocs, 222,092 frees, 11,874,846 bytes allocated
==4645==
==4645== All heap blocks were freed -- no leaks are possible
==4645==
==4645== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

Running the resource scripts non-interactively (real, captured output):

```
$ ./lisp resources/recursion.lisp
"factorial 10 =" 3628800
"fib 15 =" 610
"sum 1..100 =" 5050

$ ./lisp resources/closures.lisp
"add5 1 =" 6
"add10 1 =" 11
"add5 1 after redefining global n =" 6
"counter-a called three times (stays 1, not 1 2 3):" 1 1 1
"add3 1 2 3 curried =" 6
```

`valgrind --leak-check=full --error-exitcode=1 ./lisp resources/recursion.lisp`
and the same against `closures.lisp` both end with:

```
==5155== HEAP SUMMARY:
==5155==     in use at exit: 0 bytes in 0 blocks
==5155== All heap blocks were freed -- no leaks are possible
==5155== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

(closures.lisp: `1,472 allocs, 1,472 frees, 90,683 bytes allocated`, also
0 errors.) A third valgrind pass piped a mix of a parse error, a
division-by-zero, a zero-arg call, a real function call, a lambda call, an
unbound symbol, and a type error straight into the interactive REPL loop
(`run_repl`, not `run_file`) and came back with the same `0 errors from 0
contexts`, `in use at exit: 0 bytes in 0 blocks`.

## What it actually teaches

- **A globally-defined recursive function is a *guaranteed* reference
  cycle, not an edge case, and plain refcounting cannot collect it.**
  `def`/`fun` always bind into the *root* environment (`lenv_def` walks up
  to it), and a function defined at top level closes over the very
  environment it just got stored into -- root holds the function value,
  the function value holds a retained pointer back to root. My first
  version refcounted every environment uniformly; valgrind's first real
  run showed exactly five tests leaking (every test that called `fun`),
  each with the identical shape: `5 blocks definitely lost` plus tens of
  KB `indirectly lost`, all rooted at `lenv_new` inside the test that
  called `fun`. Rather than write a cycle collector, `lenv_retain`/
  `lenv_release` now treat the root (`parent == NULL`) as immortal --
  no-ops on it -- and its owner frees it exactly once at shutdown with a
  new `lenv_free_root` that bypasses the refcount entirely. Every test
  that defines a function now calls `lenv_free_root(env)` instead of
  `lenv_release(env)` for its top-level environment, and the previously
  leaking runs (`test_closure_captures_definition_env`,
  `test_local_put_does_not_leak_to_global`,
  `test_currying_partial_application`, `test_recursive_factorial`,
  `test_recursive_fibonacci`) are exactly the ones that pinned this down.
- **A lambda body is parsed as a Q-expression (so `\`/`fun` don't
  evaluate it immediately), and forgetting to flip it back to an
  S-expression before calling it doesn't crash or error -- it just
  silently returns the unevaluated body.** My first `lval_call` did
  `lval_eval(fun_env, lval_copy(f->body))` directly; since `lval_eval`
  only special-cases `LVAL_SYM` and `LVAL_SEXPR` and returns anything
  else unchanged, calling `((\ {x y} {+ x y}) 3 4)` printed the literal
  list `{+ x y}` instead of `7` -- no error value, no distinguishing
  signal, just the wrong-looking answer. The fix is one line
  (`body_copy->type = LVAL_SEXPR;` before `lval_eval`, mirroring what
  `builtin_if` and `builtin_eval` already did to their own Q-expression
  arguments), but it's exactly the kind of silent-wrong-answer bug that
  only a test asserting the *actual returned number* catches --
  `test_recursive_factorial`/`fibonacci` and
  `test_currying_partial_application` all call through user-defined
  lambdas to a specific expected integer for this reason, not just to a
  "no error" check.
- **The parser's symbol-character set has to be complete, and a single
  missing character fails silently late, not where you'd look.** `||`
  parsed as a syntax error ("unexpected character") because `|` wasn't in
  the lexer's symbol-char set (`_+-*/\=<>!&%^` -- `&` was there for `&&`,
  `|` simply got forgotten). The failure surfaces as `Error: Parse error:
  unexpected character`, which looks identical whether the bug is in the
  symbol table, the grammar, or a typo in the test input, so this one was
  only found by manually round-tripping every builtin's operator string
  through the REPL before writing the test suite; `test_parse_*` now
  exercises `||`/`&&` alongside the bracket/comment/string cases.
- **A closure captures its defining *environment*, not its defining
  *values* -- so mutating that environment later, from inside the
  closure, doesn't do what "closures have persistent state" suggests
  unless the mutation actually targets the closed-over frame.** Every
  call to a function creates a brand new child environment for that call,
  layered on top of whatever the function closed over; local `=` (`put`)
  always writes into that fresh per-call frame, never into the frame the
  closure captured. A `make-counter` that does `(= {count} seed)` once
  and returns `(\ {_} {do (= {count} (+ count 1)) count})` looks like a
  stateful counter but isn't one: `(counter-a 0)` called three times
  returns `1 1 1`, not `1 2 3`, because each call's `(= {count} ...)`
  mutates that call's own throwaway frame, reads through the parent chain
  to get the base value, and then discards the frame it just wrote to.
  `resources/closures.lisp` keeps this example specifically because it's
  the real, slightly counterintuitive result I got the first time I tried
  to write a "stateful" closure, not because it's the interesting case --
  `make-adder`, which only ever *reads* its captured `n`, works exactly
  as expected and is what `test_closure_captures_definition_env` checks
  against a second, independent closure (`add10`) and a later global
  rebinding of the same symbol name, to rule out both "wrong closure
  captured" and "resolved the symbol dynamically at call time" as
  explanations for a passing result.
- **`realloc(ptr, 0)` is legal C but valgrind flags it, and the fix
  changes the invariant `lval_pop` maintains.** Popping the last element
  of an `Lval` cons cell shrank `count` to 0 and then called
  `realloc(v->cell, 0)`, which glibc is free to implement however it
  likes; valgrind reported it every time a list-manipulating builtin ran
  (`8,024 errors from 48 contexts` in one `run_tests` pass, though `0
  bytes` were ever actually leaked by it). `lval_pop` now frees and
  nulls `v->cell` explicitly when the new count is 0 instead of calling
  `realloc` with a zero size, which also means `v->cell == NULL` is a
  reliable way to tell "empty" apart from "not yet allocated" elsewhere
  in the codebase.

## Deliberate scope cuts

- **No garbage collector.** Environments are manually reference-counted,
  and the one structural cycle this design can produce (a global
  function closing over the global environment that holds it) is handled
  by exempting the root from refcounting rather than by general cycle
  detection. A local, non-global self-referential closure (something
  defined with local `=` that captures and stores itself back into its
  *own* call frame, rather than into root) would still leak under this
  scheme -- nothing in `resources/` or `tests/` constructs that pattern,
  so it's untested and unfixed, not verified-safe.
- **No tail-call optimization.** `sum-to` in `resources/recursion.lisp`
  is written accumulator-style specifically because it's still not
  TCO'd -- every recursive call is a real C stack frame via
  `lval_eval` -> `lval_eval_sexpr` -> `lval_call` -> `lval_eval`, so deep
  enough recursion (tens of thousands of calls, depending on the C stack
  size) will exhaust the stack rather than looping.
- **No bignums.** Numbers are `double`; integers beyond 2^53 silently
  lose precision instead of erroring.
- **Zero-argument function calls can't be written directly.** `(f)`
  parses as a one-element S-expression, and `lval_eval_sexpr` collapses
  any one-element S-expression to that element without calling it (this
  is inherited from the tutorial's own design, not introduced here) --
  every builtin and every lambda in this project takes at least one
  argument as a result, including the "unused" `_` parameter in the
  `make-counter` example.
- **No `&` variadic parameters.** The book's optional variadic-argument
  extension for `\` (`{x & xs}` binding the rest as a list) isn't
  implemented; every lambda has fixed arity, with currying for partial
  application.
- **No macros, no quasiquote/unquote, no continuations, no tail calls,
  no file I/O builtins beyond running a script from the command line.**
- **String escapes are limited to `\n \t \r \\ \"`** -- no `\uXXXX`, no
  octal/hex escapes.

## What I'd add next

- **`&` variadic parameters for `\`**, so a user-defined function could
  take a fixed prefix of arguments plus "the rest as a list," which the
  currying implementation here already has most of the plumbing for.
- **A `let`-style construct that creates a genuinely fresh child
  environment for a block** (rather than reusing the ambient call frame
  the way `do` does now), to make the "does mutation persist" question
  in the closures lesson above controllable instead of implicit.
- **Bignum integers** (even a simple arbitrary-precision-via-string
  approach) so `(factorial 30)` doesn't silently lose precision the way
  it currently does past `double`'s 53-bit mantissa.

## License

Licensed under the MIT License; see the LICENSE file at the repository
root. Built from ["Build Your Own Lisp"](https://www.buildyourownlisp.com/)
by Daniel Holden.
