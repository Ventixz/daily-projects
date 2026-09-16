# Build an HTML Calculator with JS (HTML/CSS)

**Source:** ["How to Build an HTML Calculator App from Scratch Using JavaScript"](https://medium.freecodecamp.org/how-to-build-an-html-calculator-app-from-scratch-using-javascript-4454b8714b98),
from the HTML/CSS section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial's brief is a four-function calculator that evaluates left to
right as you press buttons (`3+4*2` gives `14`, not `11`). I kept the "no
framework, no build step" constraint, but replaced the naive evaluation
with a real expression parser: a tokenizer, a shunting-yard precedence
parser (with unary minus as its own precedence level), and a stack
evaluator -- plus memory registers, a running history, keyboard input, and
contextual percent handling, none of which the base tutorial has.

## What it is

- `src/core/tokenizer.js` -- turns a raw string like `"12.5+3*-4"` into a
  flat token list. Knows nothing about unary minus or precedence; a `-` is
  always an operator token here.
- `src/core/parser.js` -- `normalizeUnary` reclassifies a `-` as unary
  whenever it starts the expression or follows another operator, then
  `toRPN` runs shunting-yard over the result. Also owns
  `lastBinaryOperation` (splits an expression at its final binary operator,
  for repeat-`=` and percent) and `tokensToExpression` (serializes tokens
  back to a string).
- `src/core/evaluator.js` -- stack-based RPN evaluation; `applyBinary` is
  exported separately so `session.js` can reapply a single operator/operand
  pair for repeat-`=` without re-running the whole parser.
- `src/core/format.js` -- two number-to-string functions for two different
  jobs: `formatDisplay` (final results, may use exponential notation,
  cleans up float noise) and `formatForExpression` (splices a number back
  into an editable expression, must never emit exponential notation since
  the tokenizer can't read it back).
- `src/core/session.js` -- the state machine. Ten pure reducers
  (`pressDigit`, `pressOperator`, `pressEquals`, `pressPercent`,
  `pressToggleSign`, `pressMemory`, ...), no DOM, no dependency on wall-clock
  time.
- `src/core/memory.js`, `src/core/history.js` -- small pure helpers for the
  M+/M-/MR/MC register and the capped calculation log.
- `src/ui/render.js` -- state to DOM only, one direction. Also owns the
  ASCII-to-glyph swap (`+-*/` to `+ − × ÷`) so the state itself stays in the
  tokenizer's own alphabet.
- `src/ui/keyboard.js` -- maps a `KeyboardEvent` to the same action shape
  the button click handler uses.
- `app.js` -- the only place with a `let state`; every input becomes one
  `pressX` call followed by one `render()` call.
- `tests/unit/` -- 74 hand-rolled assertions against the pure core.
- `tests/e2e/` -- 10 Playwright tests against the actual page in actual
  Chromium (button clicks, keyboard input, error state, history recall,
  memory register).

## Run it

```bash
cd 2026-09-16-html-calculator
make unit    # 74 assertions, plain node, no deps
make e2e     # 10 browser tests via the globally-installed playwright
make test    # both
make serve   # http://localhost:8080
```

## What it actually teaches

- **Reusing the tokenizer for something that isn't parsing an expression
  can leak an internal marker straight onto the screen.** Toggle-sign and
  percent both round-trip the trailing number through
  `tokenize` -> `normalizeUnary` -> `tokensToExpression` instead of doing
  string surgery, so they share the exact same "what counts as this
  number's sign" logic the parser uses. `normalizeUnary` marks a leading
  minus with an internal token value of `"u-"` (distinct from the real `"-"`
  character, precisely so shunting-yard can give it its own precedence).
  The first version of `tokensToExpression` just wrote `t.value` straight
  into the output string for any operator token -- which is correct for a
  real `+-*/` character, but for a `u-` token it prints the literal text
  `"u-"`. Toggling the sign on `42` would have produced the string
  `"u-42"`, which the next tokenizer call rejects outright. I caught it
  tracing `pressToggleSign` by hand before ever running a test, but
  `session.test.js`'s `toggle sign on a plain number adds a leading unary
  minus` would have failed immediately and unambiguously if I'd shipped it
  -- `"u-42"` next to an expected `"-42"` is not a subtle mismatch.
- **Percent isn't a grammar symbol, because its meaning depends on the
  operator next to it.** `100+10%` means "10% of the first operand" (10),
  so the whole expression is 110. `100*10%` means "10% as a plain
  multiplier" (0.1), so the whole expression is 10 -- the same `10%`
  keystroke means two different numbers depending on context. `pressPercent`
  handles this by never introducing a `%` token into the grammar at all: it
  finds the trailing `[operator, number]` pair with `lastBinaryOperation`,
  computes the right value for that operator, and **rewrites the plain
  number in the expression string** before handing it back to the ordinary
  tokenizer/parser. `session.test.js` has both cases side by side
  (`percent after '+'...` vs. `percent after '*'...`) specifically because
  they'd both pass if percent were implemented as "always divide by 100" --
  only comparing the two contexts against each other exposes that the
  additive case needs the first operand and the multiplicative one doesn't.
- **A float midpoint bug doesn't need a giant number to show up.**
  `0.1 + 0.2 === 0.30000000000000004` in JS, and a calculator that just
  called `.toString()` on results would show that noise on screen after the
  most common possible keystroke sequence. `formatDisplay` rounds through
  `toPrecision(12)` before stringifying, and `format.test.js`'s
  `formatDisplay: cleans up classic float noise` computes `0.1 + 0.2` (not
  a hand-picked "ugly" literal) specifically so the test breaks if that
  rounding step is ever removed.
- **Two "clear" operations that sound almost identical need genuinely
  different implementations.** CE (clear-entry) removes only the number
  currently being typed, keeping earlier terms: `"12+34"` with CE becomes
  `"12+"`. AC (all-clear) resets the whole calculation but deliberately
  *keeps* memory and history -- clearing today's arithmetic shouldn't erase
  a number you stashed in memory for later, or the log of what you already
  computed. `session.test.js`'s `AC resets the calculation but keeps memory
  and history` asserts on `state.memory` and `state.history.length` after
  `pressClear`, not just on the screen text, because a version that reset
  everything would still show the same `"0"` on screen and look identical
  from the display alone.
- **Repeat-`=` needs to remember an operator/operand pair *before* the
  expression that produced it is gone.** Real calculators let you press
  `5+3=` to get `8`, then press `=` again to get `11` (it reapplies `+3`),
  again for `14`, and so on -- but by the time the second `=` is pressed,
  the original expression string has already been replaced by the result
  `"8"`. `pressEquals` solves this by extracting `{operator: "+", operand:
  3}` via `lastBinaryOperation` at the *same moment* it computes the first
  result, stashing it in `state.lastOp` for any later bare `=` press to
  reapply via `evaluator.js`'s standalone `applyBinary`. `session.test.js`'s
  `repeat-= twice keeps reapplying` presses `=` three times in a row and
  checks `8`, then implicitly `11`, then the final `14`, because a version
  that only handled *one* repeat (by, say, clearing `lastOp` after using it
  once) would pass a single-repeat test but fail the second one.

## Deliberate scope cuts

- **No parentheses.** The grammar is flat infix with precedence, which
  covers everything a four-function calculator's buttons can produce; a
  `(` `)` pair would need its own shunting-yard handling and its own set of
  keys this project doesn't have.
- **`=` right after a dangling operator is a no-op**, not "replay the left
  operand as its own right-hand side" (some hardware calculators treat
  `5+=` as `5+5=10`). `pressEquals` just leaves the state unchanged when the
  expression ends in an operator with no operand yet.
- **Toggling sign or taking percent of a number `>= 1e21`** can't be
  re-embedded as a plain decimal (`toFixed` itself falls back to
  exponential past that point, per spec) -- the resulting expression is
  syntactically invalid and surfaces as `Error` on the next `=` rather than
  being handled specially.
- **No undo for a completed calculation**, only for in-progress digit entry
  (backspace/CE). History is read-only except for "click to recall."

## What I'd add next

- **Parenthesized sub-expressions**, extending shunting-yard with `(`/`)`
  handling and two more keys.
- **A settings toggle for angle mode and a couple of scientific functions**
  (`sin`/`cos`/`sqrt`) -- the RPN evaluator already generalizes to unary
  functions the same way it handles `u-`, so this is additive rather than a
  rewrite.
- **Persisting history across a reload** (this project's `2026-09-06`
  sibling already has the IndexedDB-with-migration pattern this would
  reuse).
