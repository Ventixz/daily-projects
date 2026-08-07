# Write Yourself a Scheme in 48 Hours (Haskell)

**Source:** ["Write Yourself a Scheme in 48 Hours"](https://en.wikibooks.org/wiki/Write_Yourself_a_Scheme_in_48_Hours)
(Haskell: Interpreters/Compilers section), from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
I built against the tutorial's overall shape -- Parsec-based reader, `IORef`-chain
environment, `ExceptT`-based error handling -- but wrote every module from scratch
against my own understanding of how a metacircular-style interpreter has to fit
together, not by transcribing the tutorial's code. No package manager was available
in this environment, so everything here is built against exactly what GHC 9.4.7
ships as boot packages (`base`, `parsec`, `mtl`, `containers`) -- no `cabal`, no
network fetch of a single dependency.

## What it is

A Scheme interpreter with a REPL and a file runner, split the same way the
tutorial structures it:

- `src/Types.hs` -- the `LispVal` data type (atoms, lists, dotted lists, numbers,
  strings, bools, primitive/IO/user functions), the `LispError` type, and the two
  monads everything runs in: `ThrowsError = Either LispError` for pure primitives,
  `IOThrowsError = ExceptT LispError IO` for anything that touches the environment.
- `src/Parser.hs` -- a Parsec reader: atoms, strings with escapes, decimal/hex/
  octal/binary numbers, `'`/`` ` ``/`,` sugar, `;` line comments, lists and dotted
  lists.
- `src/Env.hs` -- the mutable environment: `IORef [(String, IORef LispVal)]`, so
  both "what names exist" and "what a name is bound to" can change after the fact.
- `src/Eval.hs` -- the evaluator: self-evaluating literals, `quote`/`quasiquote`,
  `if`/`cond`/`case`, `define`/`set!`, `lambda` (fixed, dotted, and fully variadic
  parameter lists), `let`/`let*`, `begin`/`and`/`or`, and function application.
- `src/Primitives.hs` -- arithmetic, comparisons, string/list ops, type predicates,
  `eqv?`/`equal?`, and the IO primitives (`display`, `write`, `newline`, `apply`).
- `src/Repl.hs` / `src/Main.hs` -- the REPL loop and a file runner that evaluates
  every top-level form in one shared environment.

## Run it

```bash
cd 2026-08-07-haskell-scheme
make test              # 76 cases, zero test-framework dependencies
make repl               # scheme> prompt
make run FILE=examples/fib.scm
```

`tests/Spec.hs` has no Hspec/HUnit available to it (no package manager to fetch
them with), so it's a ~40-line hand-rolled runner: each case evaluates Scheme
source against a fresh environment and compares the printed result against an
expected string, which doubles as a spec of exactly what the REPL prints.

## What it actually teaches

- **A mutable, two-level environment is what makes closures, `set!`, and mutual
  recursion between internal `define`s all the same mechanism.** `Env.hs`'s
  `getVar`/`setVar` read or overwrite the `IORef LispVal` a name already points
  to; `defineVar` is the only one allowed to splice a *new* `(name, IORef)` pair
  onto the front of the environment's own `IORef`. A closure created by `lambda`
  just stores the `Env` it was built in (`Types.hs`'s `Func` record). Put those
  two facts together and you get, for free: `set!` inside a closure mutates state
  the *next* call to that closure (or another closure over the same env) will see;
  and two `define`s inside one function body, evaluated in order, end up in the
  *same* `Env`, so the first one's closure can call the second by name even though
  it didn't exist yet at the moment the first closure was built -- by the time
  it's actually *called*, it does. The test
  `"internal defines can mutually recurse, because both closures share the same
  mutable call-frame Env"` (`tests/Spec.hs`) pins exactly this: `my-even?` and
  `my-odd?`, defined in sequence inside one function body, call each other
  correctly with no `letrec` special form anywhere in the evaluator. I did not
  expect two `define`s and a hand-rolled `IORef` chain to be a sufficient
  `letrec` -- it falls out of "define mutates the enclosing frame in place"
  entirely by accident of how the rest of the machinery already had to work.
- **Two closures from the same factory function don't share state, because
  `bindVars` builds a fresh `Env` per call rather than reusing one.** Every call
  to a user function runs `bindVars clos (zip params args)`, which allocates a
  brand new list of `IORef`s and a brand new outer `IORef` to hold them --
  `make-counter` called twice produces two distinct `n` bindings even though both
  calls run the identical closure-creation code. The test `"two counters from the
  same maker do NOT share state"` calls one counter three times, then a second
  one once, and asserts the second one's first call is `1` -- if `bindVars`
  mutated a shared frame instead of allocating fresh `IORef`s, that would come
  back `4`.
- **Weakly-typed arithmetic is a specific, small decision, not an accident.**
  `unpackNum` in `Primitives.hs` accepts a `Number` directly, but also a `String`
  that parses as one (`reads s`) -- so `(+ 2 "3")` evaluates to `5`. That's the
  tutorial's signature "gotcha," and building it made the real lesson obvious: the
  permissiveness lives in exactly one function that every arithmetic primitive
  routes through, so the entire language's numeric-coercion policy is a one-line
  decision to audit or change, not something scattered across every operator.
- **`equal?`'s cross-type coercion needs an existential type, because Haskell
  functions are monomorphic but "try unpacking as a number, then a string, then a
  bool" needs one loop over three different concrete unpackers.** `Primitives.hs`
  wraps each `LispVal -> ThrowsError a` unpacker in `data Unpacker = forall a. Eq a
  => AnyUnpacker (...)`, so `mapM (unpackEquals a b) [AnyUnpacker unpackNum, ...]`
  can iterate a *list* of functions whose `a` differs per element -- something a
  plain `[LispVal -> ThrowsError SomeFixedType]` can't express. `eqv?` skips all of
  this and uses `lispEqv` (plain structural equality, no coercion) directly, which
  is also what `case` uses to match a key against a clause's datum list -- sharing
  that one function is what keeps `case` and `eqv?` from silently drifting apart
  on what "the same" means.
- **Two parser bugs only showed up when I ran real multi-line files, not the
  single-line REPL tests I'd been checking against as I wrote each special form.**
  Both are pinned as regression tests now:
  - No comment support, combined with `sepEndBy`'s "parse as many as you can, then
    stop" semantics and `readExprList` not requiring `eof`, meant a file starting
    with a `; comment` line parsed to *zero* top-level forms and the interpreter
    exited `0` having silently done nothing -- not a crash, not an error message,
    just a program that looked like it ran and didn't. `examples/fib.scm` was the
    file that caught it: `make run FILE=examples/fib.scm` printed nothing at all.
    The fix is two-part -- `Parser.hs` gained an actual `;`-to-end-of-line comment
    parser, and `parseTopLevel` now parses `sepEndBy parseExpr padding` and then
    requires `eof`, so anything left over is a loud parse error instead of a
    silently-truncated program. `"line comment at start of a form"` and `"line
    comment is skipped, not a parse error"` in `tests/Spec.hs` both exercise this.
  - Fixing "list with trailing whitespace before `)`" and "dotted list" at the
    same time turned out to be in genuine tension. `sepBy`/`endBy` fail hard
    (consumed-input-then-failed, which Parsec won't backtrack without `try`) on a
    trailing separator -- which is *exactly* what made `try parseList <|>
    parseDottedList` select the dotted-list branch on input like `(1 2 . 3)`
    (parsing "1 2 " as a list hits the hard failure right at the `.`, `try`
    catches it, `parseDottedList` runs instead). But that same hard-fail-on-
    trailing-separator behavior means `(1 2 3 )` -- a trailing space before a
    perfectly ordinary close paren -- failed to parse at all. Switching to
    `sepEndBy` (which tolerates a trailing separator gracefully) fixes the
    whitespace case but breaks the dotted-list case, because `sepEndBy` now
    happily accepts "1 2 " as a *complete* list and never gives the dotted-list
    branch a chance to run. The actual fix was to stop relying on backtracking to
    distinguish the two forms at all: `parseListOrDotted` parses the elements once
    with `sepEndBy`, then explicitly checks whether a `.` follows, and builds
    a `List` or `DottedList` based on that check rather than on which parser
    happened to fail.
- **`quotient`/`remainder`/`mod` route through Haskell's own division operators,
  which throw an `ArithException` -- not a value in `ThrowsError` -- on a zero
  divisor.** Nothing in `ExceptT LispError IO`'s error handling catches a raw
  Haskell exception; it would have taken the whole process down, not just failed
  the one Scheme expression. `numericBinop` checks the divisor against `0` and
  throws a proper `LispError` before ever calling `quot`/`rem`/`mod`, so `(mod 5
  0)` is a catchable `LispError` an interpreter user sees as an error message, not
  a crash. `(quotient 5 0)` and `(mod 5 0)` are both pinned as tests specifically
  because I found this the hard way (via a mediocre-looking Haskell stack trace)
  before adding the guard.
- **`getLine` throwing on EOF is a real, everyday case, not an edge case to
  shrug off.** Piping input to the REPL (`echo '(+ 1 2)' | ./bin/scheme`, which is
  how the test-by-hand loop above actually runs) or pressing Ctrl-D both end with
  `getLine` throwing an `IOException` once stdin is exhausted -- uncaught, that's
  an ugly `hGetLine: end of file` crash instead of a clean exit. `readPrompt` in
  `Repl.hs` catches it with `Control.Exception.catch`, checks `isEOFError`, and
  feeds the REPL loop the same `"(quit)"` sentinel a normal exit uses.

## What I'd add next (stretch goals I skipped for scope)

- **Proper tail calls.** `examples/fib.scm` runs `fib 24` specifically because
  it's slow enough to *see* the lack of memoization and TCO -- every recursive
  call is a real Haskell call, so a tail-recursive Scheme loop over a large range
  would blow the Haskell stack. Real Scheme guarantees tail-call elimination;
  this interpreter doesn't attempt it.
- **`letrec` as its own form.** Internal `define` inside a function body already
  gives mutual recursion by accident (see above), but there's no `letrec`/
  `letrec*` for the case where you want that inside a `let`-shaped block instead
  of a whole function.
- **Rationals and floats.** Every number is an `Integer`; `(/ 1 3)` truncates to
  `0` via `quot` rather than producing a rational or a float. Scheme's actual
  numeric tower (exact/inexact, rational, complex) is a large chunk of the spec
  I deliberately didn't attempt.
- **`call/cc`.** First-class continuations are the single most distinctive
  feature real Scheme has over most other languages, and also the one hardest to
  retrofit onto a Haskell evaluator that isn't already in CPS.
- **Nested quasiquote.** `evalQuasiquote` only splices a top-level `unquote`
  inside a quasiquoted list; nested `` ` `` / `,` depth tracking (so `,` only
  escapes as many levels as it's nested) is the part of quasiquote that's
  actually subtle, and I scoped it out.
- **File I/O primitives.** `open-input-file`/`open-output-file`/ports are in the
  original tutorial's IO primitives list; I kept the IO surface to `display`/
  `write`/`newline`/`apply`, enough to make `examples/*.scm` legible without
  touching the filesystem.
