# Write You a Haskell (Haskell)

**Source:** ["Write You a Haskell - Build a modern functional compiler"](https://web.archive.org/web/2021/http://dev.stephendiehl.com/fun/)
by Stephen Diehl, from the Haskell section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
(The tutorial builds a much larger Haskell-subset-to-LLVM compiler over many
chapters; the part that's the actual point for a one-sitting build is its
early chapters -- lexer, parser, and Hindley-Milner type inference via
Algorithm W -- so that's the scope here, paired with a tree-walking
evaluator instead of LLVM codegen so the whole pipeline runs end to end.)

No dependencies beyond `base` -- no Alex/Happy, no `mtl`, no `hspec`. Every
piece (lexer, parser combinators, unifier, evaluator, test runner) is
written from scratch specifically so `ghc` alone, with no Cabal/Hackage
fetch, can build and test this.

## What it is

`Poly`, a tiny statically-typed functional language, implemented as a
straight pipeline: source text → tokens → AST → inferred type → value.

- `src/Lexer.hs` -- hand-rolled tokenizer (no generator).
- `src/Parser.hs` -- a ~15-line parser-combinator library (`Functor`/
  `Applicative`/`Monad`/`Alternative` instances for a `Parser` newtype),
  then a recursive-descent grammar built on top of it with the usual
  precedence tower (`||` < `&&` < comparisons < `+`/`-` < `*`/`/` <
  application).
- `src/Type.hs` + `src/Infer.hs` -- Hindley-Milner type inference,
  Algorithm W: substitutions, unification with an occurs check, and
  `generalize`/`instantiate` for let-polymorphism.
- `src/Eval.hs` -- a small tree-walking evaluator with closures.
- `app/Main.hs` -- runs a `.poly` file or a line-at-a-time REPL: lex,
  parse, type-check, and only evaluate if type-checking succeeded.

The language: integers, booleans, `if`, `let`/`let rec`, lambdas
(`\x -> ...`), application, and arithmetic/comparison/boolean operators.
No type annotations anywhere -- every type in every example below is
inferred.

## Run it

```bash
cd 2026-09-25-haskell-poly-lang
make build              # ./bin/poly
make test               # ./bin/test-runner, 22 checks across all 4 stages
make run                # runs examples/factorial.poly
./bin/poly examples/polymorphism.poly
./bin/poly               # REPL
```

## What it actually teaches

- **Let-polymorphism is a property of `let`, not of function values.**
  `examples/polymorphism.poly` defines `id` once with `let id = \x -> x`
  and then applies it to a `Bool` and later to an `Int` in the same
  program -- legal, because `generalize` in `Infer.hs` runs at the `let`
  and closes over every free type variable the environment doesn't
  already mention, producing `forall a. a -> a`; each *use* of `id`
  then calls `instantiate` to mint a fresh copy of that variable. Rewrite
  the same trick through a lambda binding instead of `let`
  (`(\id -> if id true then id 1 else id 0) (\x -> x)`) and it's a type
  error -- `ELam` in `infer` binds the parameter as `Forall [] tv`, one
  concrete (if unknown) type variable, not a scheme to instantiate. A test
  in `tests/Spec.hs` pins down that the `let` version type-checks and the
  lambda version doesn't, specifically so this distinction can't quietly
  regress.

- **Unification is substitution composition, and composition order is
  not a formality.** `composeSubst s1 s2` has to mean "apply `s2`, then
  `s1`" -- it's defined as `Map.map (apply s1) s2 \`Map.union\` s1`, and
  every call site in `Infer.hs` (`unify`'s `TFun` case, `infer`'s `EApp`,
  `EIf`, `EBinOp` cases) has to compose in the same right-to-left order
  the sub-inferences actually ran in. Getting this backwards doesn't
  crash -- it produces types that are subtly wrong on programs with more
  than one operator, which is a worse bug than a crash and the reason
  `tests/Spec.hs` checks concrete inferred types (`Int -> Int -> Int` for
  curried `add`) rather than just "does it type-check."

- **The occurs check is the difference between "no such type" and an
  infinite loop.** `varBind` refuses to bind a type variable `a` to a
  type that mentions `a` (e.g. what `\x -> x x` would demand: `a` unifies
  with `a -> b`). Skip that check and `apply`, which recurses into
  `TFun`'s arguments, no longer terminates on such a substitution --
  self-application is exactly the case a real HM implementation has to
  reject, and now there's a one-line test (`typeOf "\\x -> x x"` is a
  `Left`, elsewhere) proving it's rejected rather than hung.

- **A well-typed program can't get stuck, and `Eval.hs` is written to
  take that literally.** `Main.hs` only calls `eval` after `inferExpr`
  returns `Right`. Because of that, every partial case in `eval` --
  applying a non-closure, branching on a non-`Bool`, an unbound variable
  -- is genuinely unreachable from any program that passed the type
  checker, not just unlikely; the comments on those cases say exactly
  that instead of pretending they're routine error handling. It's the
  same "boring runtime because the type checker did the work" that makes
  Hindley-Milner worth the trouble in the first place.

- **Recursion doesn't need a mutable cell in a lazy language.**
  `ELetRec`'s evaluation is `let env' = Map.insert f (eval env' e1) env in eval env' e2`
  -- `env'` is defined in terms of itself. This works because
  `Map.insert` doesn't force its value argument and evaluating a lambda
  (`eval env (ELam x body) = VClosure x body env`) doesn't force the
  environment it closes over -- it just stores a reference to the thunk.
  By the time `fact` is actually *called*, the recursive reference inside
  its own closure has long since been resolved to itself, with no `IORef`
  or Y-combinator in sight. `tests/Spec.hs`'s `fib`/`fact` tests exercise
  this multiple calls deep (`fib 10` recurses to depth 10) specifically
  to make sure the tied knot survives more than one level of self-call.

## Known limitations (by design, to keep scope to ~2-4 hours)

- No type annotations, no polymorphic recursion, no algebraic data types
  or pattern matching -- the language is deliberately just enough to make
  let-polymorphism and unification demonstrable, not a general-purpose
  language.
- No LLVM/bytecode codegen, unlike the tutorial's eventual destination --
  the evaluator is a tree-walking interpreter over the AST directly.
- Comparisons (`<`, `<=`, `==`) are non-associative by the grammar (one
  operator per expression, no chaining) rather than desugared to
  conjunctions the way some MLs do it.
- No source locations on parse/type errors -- messages name the mismatch,
  not a line/column.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Credit: ["Write You a Haskell"](https://web.archive.org/web/2021/http://dev.stephendiehl.com/fun/)
by Stephen Diehl.
