# Building a Spell-Checker (Clojure)

**Source:** ["Building a Spell-Checker"](https://bernhardwenzel.com/articles/clojure-spellchecker/),
from the Clojure section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
This environment's network only reaches GitHub and a short allowlist of
package registries -- the article itself is blocked -- so what's here isn't
a port of that specific post. It's the algorithm every Clojure spell-checker
tutorial like it is itself built on: Peter Norvig's ["How to Write a
Spelling Corrector"](https://norvig.com/spell-correct.html) (also blocked,
but well-known enough to rebuild from memory), translated into idiomatic
Clojure rather than transliterated from his 21 lines of Python.

## What it is

Norvig's insight: you don't need a real edit-distance/noisy-channel model to
get useful spelling correction. Generate every string one or two edits away
from the input (deletions, adjacent-letter transpositions, substitutions,
insertions), keep only the ones that are actual words in a corpus, and
return whichever candidate appears most often in that corpus.

- `src/spell_checker/model.clj` -- tokenizes a text into lowercase words
  and counts them into a `word -> frequency` map. `resources/corpus.txt` is
  *Pride and Prejudice* + *Moby Dick* concatenated (both public domain),
  pulled from GITenberg mirrors on `raw.githubusercontent.com` since
  `gutenberg.org` itself isn't reachable here. 19,075 distinct words,
  347,467 tokens total.
- `src/spell_checker/correct.clj` -- `edits1`, `edits2`, `known`,
  `candidates`, `correction`, plus `correct-word`/`correct-text` for
  case-preserving, punctuation-preserving correction of whole strings.
- `test/` -- 10 `clojure.test` deftests, 34 assertions, run with a
  five-line hand-rolled runner (see below for why).

## Run it

```bash
cd 2026-08-16-clojure-spell-checker
make test                                    # 10 tests, 34 assertions
make run ARGS="speling teh wich"             # one-off word corrections
echo "Speling errors HAPPEN." | make run     # whole-text correction via stdin
```

## What it actually teaches

- **Leiningen's own bootstrap dependency was unreachable, which meant
  abandoning it entirely rather than working around it.** `apt-get install
  leiningen` and `apt-get install clojure` both succeeded, but `lein run`
  failed immediately trying to resolve `nrepl:nrepl:1.0.0` -- a dependency
  of Leiningen itself, not of this project -- from `repo.clojars.org`,
  which this environment's proxy returns a flat `403` for on every request,
  including ones Debian's own `libnrepl-clojure` package had already put on
  disk. There's no `:offline?` flag or local-repo override that fixes a
  *transitive* dependency Leiningen decides it needs before your `project.clj`
  is even read. The fix wasn't a workaround, it was a different tool: the
  Debian `clojure` package turned out to be a thin shell script
  (`/usr/bin/clojure1.11`) around `java -cp clojure-1.11.jar clojure.main`
  with no deps.edn/Maven resolution layer at all -- so nothing it does
  touches the network, and a plain `-cp src:resources` classpath is
  sufficient to run the whole project. `Makefile` wraps that classpath
  invocation the same way `2026-08-09-java-http-server`'s `Makefile` wraps
  `javac`/`java` directly, for the same reason: no network-dependent build
  tool available, so don't route around it, just don't need one.
- **`clojure.test/run-tests` returns the summary it prints, and that's
  enough to make `make test` fail correctly with zero extra dependencies.**
  With no `clj -X` test runner or `cognitect.test-runner` reachable (same
  Clojars block), `test/spell_checker/test_runner.clj` requires the two
  test namespaces and calls `(clojure.test/run-tests 'ns1 'ns2)` directly.
  Its return value is the `{:test :pass :fail :error}` map it already
  printed -- reading `:fail`/`:error` back out of that and calling
  `System/exit` on the result was all `make test` needed to report failure
  as a nonzero exit code the normal way, no additional library required.
- **Two equally-plausible corrections don't tie -- `correction` breaks
  every tie by corpus frequency, including in favor of the wrong word.**
  `"wich"` is one edit from both `"which"` (insert an `h`) and `"with"`
  (substitute `c`→`t`), and both are real words in the corpus, so
  `candidates` returns both. `correction` picks whichever
  has the higher count with no other signal -- and `"with"` outnumbers
  `"which"` 2,870 to 1,201 in Austen and Melville combined, so `(correction
  freqs "wich")` returns `"with"`, silently wrong for what was almost
  certainly meant as `"which"`. `correct_test.clj`'s
  `correction-test` asserts this outcome deliberately rather than treating
  it as a bug: it's the honest behavior of ranking by raw frequency alone,
  and it's exactly the gap Norvig's fuller model (which weights by edit
  probability, not just word frequency) closes and this 21-line version
  doesn't.
- **A same-letter replace is a no-op, so `edits1(word)` always contains
  `word` itself -- and that's not a bug to filter out, it's *why*
  `candidates` doesn't need a separate already-correct check.** Of the
  `26n` replace results, exactly one per position substitutes a letter for
  itself, and all `n` of those collapse to the same string: the original
  word. `correct_test.clj` asserts `(contains? (edits1 "cat") "cat")`
  directly. Once that's true, `candidates`' first branch, `(known model
  [word])`, is really just special-casing the zero-edit distance for
  speed -- correctness doesn't depend on it, since a known word would
  survive into its own `edits1` set and get selected by `known` there too.
- **`edits1("something")` has exactly 494 unique members, not the
  511 raw deletes+transposes+replaces+inserts you'd get by only summing
  `n + (n-1) + 26n + 26(n+1)`,** and working out where the 17-element gap
  goes is what actually shows how the four generators overlap. All 9
  same-letter replace duplicates collapse into the 1 original word
  (`234 → 226`, an 8-element drop). Every one of "something"'s 9 distinct
  letters produces exactly one insert/delete-boundary collision -- inserting
  letter `X` right before an occurrence of `X` in the word equals inserting
  it right after (`260 → 251`, a 9-element drop). `8 + 9 = 17`, and deletes
  and transposes contribute no duplicates of their own since "something"
  repeats no letters. `edits1-test` pins the total; the derivation is the
  actual finding, not the number.
- **The corpus is the whole spelling model, so what it doesn't contain
  matters as much as what it does.** `"korrectud"` and `"bycycle"` are
  textbook two-edit typos for `"corrected"` and `"bicycle"` -- and this
  corrector leaves both unchanged, because neither target word occurs even
  once in *Pride and Prejudice* or *Moby Dick* (bicycles postdate both by
  decades). `known` only accepts words already in the frequency map, so a
  correct-but-absent target is indistinguishable from a nonword -- the
  model only knows what its two novels happened to mention.

## Deliberate scope cuts

- **No probability weighting by edit type or position.** Norvig's fuller
  treatment (beyond the 21-line version) trains actual `P(typo|word)`
  weights from a corpus of real misspellings; this implementation treats
  every edit as equally likely and ranks candidates purely by how common
  the corrected word is, which is what produces the `"wich"` → `"with"`
  miss above.
- **`edits2` always fully materializes.** `(set (mapcat edits1 (edits1
  word)))` builds the whole ~100K-500K-element set before `known` filters
  it, rather than filtering lazily as it's generated (which Norvig's
  actual optimized version does). Fine at this corpus size and word
  lengths; would matter for longer words or a much larger dictionary.
- **No edits3+ fallback.** A word more than two edits from anything known
  is returned unchanged rather than flagged as "no correction found" --
  there's no way from the output alone to tell "already correct" apart
  from "uncorrectable."

## What I'd add next

- **A real typo-frequency table** (even a small hardcoded one for common
  substitution/transposition patterns) to break frequency ties like
  `"wich"` correctly instead of by raw popularity.
- **A larger, more contemporary corpus** blended in alongside the two
  novels, specifically to close gaps like `"bicycle"` that are really
  "this word didn't exist yet," not modeling failures.
- **Whole-sentence context.** Right now every word is corrected in
  isolation; a bigram/trigram model over the same corpus could disambiguate
  cases where the surrounding words make the intended word obvious even
  when `correction` alone picks the wrong same-edit-distance candidate.
