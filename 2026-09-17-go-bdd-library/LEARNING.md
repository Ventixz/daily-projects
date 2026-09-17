# How to Use Godog for Behavior-driven Development in Go (Go)

**Source:** ["How to Use Godog for Behavior-driven Development in Go"](https://semaphoreci.com/community/tutorials/how-to-use-godog-for-behavior-driven-development-in-go),
from the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial's brief is "wire Gherkin scenarios to Go step definitions with
godog." I built a small book-lending domain (borrow, return, overdue fees)
specifically because it has more than one interacting rule -- a copy count,
a per-member limit, and a two-sided return -- so the BDD layer has to do
real work distinguishing which rule broke, not just report pass/fail on a
single function.

## What it is

- `internal/library/library.go` -- the domain, with no I/O and no
  dependency on godog or any framework. `Borrow` checks copy availability
  and the member's limit in that order; `Return` distinguishes "never
  borrowed" from "already returned" by checking loan history, not just
  "is there an active loan." `OverdueFee` is a free function (not a method)
  since it only ever needs a `*Loan` and a point in time.
- `internal/library/library_test.go` -- ordinary `go test` unit tests
  against the domain directly, table-driven for `OverdueFee`'s boundary
  cases.
- `features/*.feature` -- the executable spec, in Gherkin: `borrowing.feature`,
  `returning.feature`, `overdue_fees.feature`. `overdue_fees.feature` uses a
  Scenario Outline with an Examples table instead of four near-identical
  scenarios.
- `test/bdd/steps_test.go` -- `InitializeScenario` registers every step
  regex; a `world` struct (fresh per scenario, via `sc.Before`) holds the
  `*library.Library` plus the last action's result, since a `Then` step
  runs as a separate call from the `When` step whose outcome it's checking.
  `TestFeatures` is the one `go test` entry point that drives all three
  `.feature` files through `godog.TestSuite`.
- `main.go` -- a small runnable demo (seed two members, one book, walk
  through a blocked borrow and a return) so there's something to run
  outside of `go test`.
- `vendor/` -- godog and its transitive deps, vendored so `go build`/`go test`
  work offline; Go auto-detects `vendor/modules.txt` and uses it without
  needing `-mod=vendor` on the command line.

## Run it

```bash
cd 2026-09-17-go-bdd-library
make unit    # go test ./internal/... - pure domain logic
make bdd     # go test ./test/bdd/... -v - runs every .feature file, pretty-printed
make test    # both
make run     # builds and runs the CLI demo
```

## What it actually teaches

- **A step definition should translate, not judge.** Every step in
  `steps_test.go` is one or two lines: call the domain, stash the result in
  `world`. The *assertions* live in the `Then` steps
  (`theBorrowShouldSucceed`, `theActionShouldFailWith`, ...), which read
  `world` and return an error if it doesn't match. Putting a business rule
  check inside a `Given`/`When` step instead of the domain would mean the
  Gherkin layer and the unit tests could disagree about what "borrowing
  works" means -- they'd be checking two different implementations of the
  same rule.
- **Distinguishing "never happened" from "already happened" needs its own
  history, not just current state.** `Return`'s two error paths
  (`ErrNotBorrowedByMember` vs `ErrAlreadyReturned`) look identical from
  `findActiveLoan` alone -- both return `nil`. `hasReturnedLoan` walks the
  full loan slice (including closed loans) specifically so `Return` can
  tell them apart, and `returning.feature`'s two scenarios
  ("never borrowed" vs. "already returned") assert on the *exact* error
  string, so a version that collapsed both cases into one generic
  "can't return this" message would fail the feature file even though the
  action still correctly refused the return.
- **A Scenario Outline's Examples table is a parser-level feature, not
  string interpolation you write yourself.** `overdue_fees.feature`'s
  `<days>` and `<fee>` placeholders get substituted by godog before the
  step regexes ever see the text -- `theFeeIsCalculatedDaysAfterBorrowing`
  and `theOverdueFeeShouldBeCents` are plain single-value step functions
  that have no idea they're being called four times with different
  numbers. Writing four separate `Scenario:` blocks by hand would work
  identically at runtime but would drift the moment someone edits one
  case and forgets the other three.
- **"On day N" has to mean the same instant to both the step that creates
  the loan and the step that checks it later.** `borrowsOnDay` and
  `theFeeIsCalculatedDaysAfterBorrowing` both compute from the loan's own
  `BorrowedAt` (via `AddDate`), not from `time.Now()` -- a scenario that
  read the wall clock would pass today and fail intermittently depending
  on when `go test` happens to run relative to a day boundary.
- **`go vet`'s printf check catches a godog step-arity mismatch that
  compiles fine.** `sc.Step` takes a regex and a function whose parameter
  count must equal the regex's capture-group count; get it wrong and
  godog only fails at *test run* time, with a runtime panic, not a
  compile error. I doublechecked each regex's `(...)` count against its
  step function's parameter list by hand while writing `steps_test.go`,
  since nothing at compile time enforces they agree.

## Deliberate scope cuts

- **No persistence.** The whole `Library` lives in memory for the process's
  lifetime; there's no file or database backing it, since the tutorial is
  about the BDD wiring, not storage.
- **No concurrency guards.** `Library`'s methods aren't safe for concurrent
  callers (no mutex) -- every scenario in this project drives the domain
  from a single goroutine, so it was never exercised.
- **Loan period and late fee are constants**, not configurable per book or
  per member (real library systems vary both), since the fee scenarios only
  needed one fixed schedule to demonstrate the Scenario Outline pattern.

## What I'd add next

- **A `--tags` filter in the Makefile** (`godog -tags "@fees"` style) once
  the feature set is large enough that running a subset during
  development actually saves time.
- **A JSON/Cucumber report output** (`Format: "cucumber"` in
  `godog.Options`) for wiring into a CI dashboard, instead of only the
  human-readable pretty output.
- **A tiny HTTP layer over `library.Library`** so the same domain could
  also be driven by an end-to-end HTTP-level `.feature` file, showing BDD
  specs written against a running service rather than an in-process call.
