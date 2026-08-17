# No Magic: Regular Expressions (Scala)

**Source:** ["No Magic: Regular Expressions"](https://rcoh.svbtle.com/no-magic-regular-expressions),
from the Scala section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
This environment's network only reaches GitHub and a short allowlist of
package registries -- `rcoh.svbtle.com` gets a flat `EGRESS_BLOCKED` -- so
what's here isn't a port of that specific post. It's the algorithm the
post's title is actually pointing at: Russ Cox's ["Regular Expression
Matching Can Be Simple And Fast"](https://swtch.com/~rsc/regexp/regexp1.html)
(also unreachable, but the algorithm is well-known enough to rebuild from
memory), implemented as Thompson-construction-to-NFA compiled to a flat
instruction array and simulated breadth-first, the way Rob Pike's `grep`
and Cox's own `re1` do it -- not the backtracking approach every
mainstream regex engine (PCRE, Python's `re`, Java's `Pattern`) actually
ships.

## What it is

A regex engine for a small but real grammar -- literals, `.`, `*`, `+`,
`?`, `|`, `(...)` grouping, and `\`-escaping -- built in three stages that
mirror how a real compiler pipeline works, not how most "build a regex
engine" tutorials collapse parsing and backtracking-matching into one
recursive function:

- `src/Parser.scala` -- hand-rolled recursive-descent parser
  (`parseAlt` → `parseConcat` → `parseFactor` → `parseAtom`) producing an
  AST (`src/Ast.scala`). No parser-combinator library, even though
  `scala-parser-combinators` is sitting on the classpath from the `apt`
  install -- writing the four mutually-recursive functions by hand is
  what actually shows *why* `|` binds loosest, concatenation binds next,
  and postfix `*`/`+`/`?` bind tightest, instead of hiding that in a
  combinator's operator precedence table.
- `src/Compiler.scala` -- Thompson construction: walks the AST once and
  emits a flat `Array[Inst]` of five instruction types (`IChar`, `IAny`,
  `ISplit`, `IJmp`, `IMatch`), the same shape as a tiny bytecode VM.
- `src/Vm.scala` -- Pike's VM: simulates every live thread (just a
  program counter) in lockstep against the input, one character at a
  time, instead of recursively trying one thread to completion before
  backtracking to the next.
- `test/RegexTest.scala` -- 40 hand-rolled assertions (no ScalaTest --
  see below), including one that runs an input pattern-matched against
  itself that would take a naive backtracking engine exponential time.

## Run it

```bash
cd 2026-08-17-scala-regex-engine
make test                              # 40 assertions
make run ARGS="'(a|b)*c' cat abc xyz"  # match/search given strings
echo -e 'gray\ngroy\nthe gray fox' | make run ARGS='gr(a|e)y'
```

(`ARGS` is passed straight to `/bin/sh`, so quote a pattern containing
`(`, `|`, or `*` yourself -- `make run ARGS='(a|b)*c'` works,
`make run ARGS=(a|b)*c` doesn't, because the unquoted parens hit the
shell before `make` ever sees them.)

## What it actually teaches

- **A regex engine is a tiny VM, and `|`/`*`/`?` are all the same two
  instructions in different wiring.** `ISplit(x, y)` (try both `x` and
  `y`) and `IJmp(x)` (goto) are the *entire* control-flow vocabulary --
  `Alt(a, b)` compiles to a `Split` choosing between two emitted blocks
  joined by a `Jmp` past the second; `Star(a)` compiles to a `Split`
  that either enters `a` and loops back to itself or exits; `Opt(a)` is
  the same `Split` without the loop-back `Jmp`; `Plus(a)` is `a` once,
  followed by a `Split` back to its own start or through. Four
  constructs, two instructions, because loop-vs-branch is a wiring
  choice, not a different primitive.
- **The one-generation-per-step visited-set is not an optimization, it's
  what keeps an empty loop from being a stack overflow.** `(a)()*(b)`
  compiles the empty group under `*` into `Split(k+1, k+2)` at index `k`
  immediately followed by `Jmp(k)` at `k+1` -- a Split whose own
  loop-back branch points at itself with zero real instructions in
  between. `addThread`'s `gen(pc) == genId` check is what makes visiting
  `k` a second time in the same step return immediately instead of
  recursing `Split(k+1,k+2) → Jmp(k) → Split(k+1,k+2) → ...` forever;
  delete that one check and `check("empty group under star is a safe
  no-op epsilon loop", ...)` in the test suite turns into a
  `StackOverflowError` instead of a passing assertion, on a pattern with
  nothing exotic in it.
- **The classic exponential-backtracking demo pattern matches instantly
  here, and the test proves *why*, not just *that*.** `(a?){20}a{20}`
  against a 20-character string of `a`s forces a backtracking engine to
  try, in the worst case, every one of `2^20` ways of assigning the 20
  optional `a`s before landing on the single all-empty assignment that
  works. `matchFrom` never tries assignments one at a time -- every
  `Split` from every `a?` gets epsilon-closed into the *same* thread list
  in the *same* step, so all `2^20` assignments are represented as one
  set of live program counters (bounded by the 61-instruction program
  length, not by how many ways there are to satisfy it) and advanced
  together. `make test` runs both this and the one-char-short rejection
  in well under the timing noise floor.
- **Leftmost-longest falls out of the algorithm's structure, and that's
  a real, visible difference from what production regex engines do.**
  `Vm.find` compiled from `a|ab` against `"ab"` returns `(0, 2)` --
  the *longer* alternative -- not `(0, 1)`, which is what Python's `re`,
  PCRE, or Java's `Pattern` all return for the same pattern, because they
  backtrack and take whichever alternative was *written first* and
  matches at all. `matchFrom` doesn't distinguish "first-written" from
  "second-written" once both threads are alive in the same `clist`; it
  just keeps advancing every surviving thread and remembers the latest
  step at which *any* of them sat on `IMatch`, so a longer match found
  later always overwrites a shorter one found earlier. Same input,
  same pattern text, genuinely different answer depending on which
  matching strategy the engine uses underneath -- not a bug in either
  approach, just a semantic choice that's usually invisible until you
  build both.
- **`scalac`/`scala` need nothing beyond the classpath -- `sbt` would
  have needed a Maven Central resolve this box can't do.** `apt-get
  install scala` pulls the 2.11.12 compiler, REPL, and standard library
  as plain jars under `/usr/share/java`, and both `scalac -d out
  src/*.scala` and `scala -classpath out regex.Main` run with zero
  network access, the same shape of fix the Clojure day landed on
  (Leiningen's `nrepl` bootstrap dependency vs. this project's `sbt`
  needing `org.scala-sbt` artifacts from `repo1.maven.org`) for the same
  underlying reason: the *compiler* is a local jar, the *build tool
  wrapping it* is what wants a package registry, so skip the build tool.
- **`ScalaTest` wasn't reachable either, and a `main` method with an
  if-statement is a complete substitute at this scale.** No `scalatest`
  package exists in `apt`, and pulling it via `sbt` hits the same wall
  as above. `TestRunner.check(name, cond)` -- increment a counter, print
  `FAIL: $name` on failure -- plus `sys.exit(1)` when `failed > 0` is the
  entire framework, and it's enough for `make test` to fail loudly and
  correctly in CI-style usage with exactly zero third-party code.

## Deliberate scope cuts

- **No counted repetition (`{n}`, `{n,m}`).** `*`, `+`, and `?` cover the
  compiler/VM mechanics completely; adding bounded counters would mean
  either unrolling them into repeated `Concat`/`Opt` nodes at parse time
  (blows up program size for large `n`) or adding counter state to
  threads (a real complexity increase to the VM's core loop). Left out
  because it's an engineering tradeoff, not a new concept.
- **No character classes (`[a-z]`, `\d`, `\w`).** `IChar`/`IAny` are the
  only two "consume a character" instructions; a class would need a
  third that tests set membership. Straightforward to add, deliberately
  cut to keep the instruction set at its minimal five.
- **`find` retries the whole VM once per start index rather than folding
  the search into one pass.** A real `grep`-style unanchored search adds
  a self-looping `.*?` prefix so *one* simulation covers every start
  position at once, in true `O(n)`. This implementation instead calls
  `matchFrom` for each of the `n+1` start positions, giving `O(n*m)`
  overall -- correct and still linear-ish for a teaching tool, but a
  real engine wouldn't re-run the whole automaton from scratch at every
  offset.
- **No capture groups.** `(...)` only groups for precedence here; it
  doesn't record what it matched. Real capture requires tagging threads
  with the state each one captured *as it goes*, since two different
  threads reaching the same `pc` can have taken different paths through
  earlier groups -- a genuinely separate feature from the matching
  engine itself.

## What I'd add next

- **Character classes**, since they're the single most common regex
  feature this cuts and the cleanest fit for a third "consume a char if
  it's in this set" instruction alongside `IChar`/`IAny`.
- **A true unanchored `find`**, folding the "try every start position"
  loop into the VM itself via a self-looping prefix, to make the
  `O(n*m)` → `O(n)` difference concrete rather than just documented.
- **Capture groups**, specifically to show what backtracking engines get
  for free (the call stack *is* the capture state) that a thread-based
  VM has to build on purpose.
