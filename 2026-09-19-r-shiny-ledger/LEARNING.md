# Build Web Apps with Shiny (R)

**Source:** ["Build Web Apps with Shiny"](http://shiny.rstudio.com/tutorial/),
from the R section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial's own examples (`runExample()`) ship inside the `shiny`
package itself and mostly demonstrate individual widgets in isolation. I
built a small but complete app instead — a household ledger — so the
project exercises the thing the tutorial is actually teaching: wiring
reactive *inputs* through a *reactive graph* to *outputs*, not just
displaying one slider.

Same network constraint as the last R entry in this repo: CRAN itself is
unreachable, so `install.packages("shiny")` isn't an option here. `shiny`
and `testthat` came from Ubuntu's archive mirror instead
(`r-cran-shiny`, `r-cran-testthat`), which *is* reachable — prebuilt
`.deb`s, no compilation, no CRAN.

## What it is

A single-page ledger: add a dated, categorized transaction (positive for
income, negative for an expense), see the running balance and a
per-category breakdown update immediately, and remove any entry by row.

- `R/ledger.R` — the entire domain model, deliberately with **no Shiny
  dependency** at all, so it can be unit-tested as plain R:
  - `add_transaction()` validates the date, description, category, and
    amount (non-empty, non-zero, parseable) and returns a new data frame
    — never mutates in place, which matters once this same function gets
    called from inside a `reactiveVal` update.
  - `remove_transaction()`, `compute_balance()`, `running_balance()`
    (cumulative sum in date order, independent of insertion order), and
    `summarize_by_category()` (aggregate + sort by magnitude).
- `app.R` — the Shiny half, and *only* the Shiny half:
  - `ledger <- reactiveVal(new_ledger())` holds the one piece of state.
  - `observeEvent(input$add, ...)` calls the pure `add_transaction()`,
    catches its validation errors with `tryCatch()` and surfaces them via
    `showNotification()` instead of crashing the session, and on success
    replaces the reactive value wholesale — the trigger for every output
    that reads `ledger()` to re-run.
  - Three outputs (`renderPrint`, two `renderTable`s) all depend on the
    same `ledger()` call, which is the actual point of the tutorial:
    Shiny's dependency tracking means none of them need to know about
    each other, only about the reactive value they read.
  - `uiOutput("remove_ui")` renders a dynamic `selectInput` sized to the
    current row count, generated server-side rather than declared once
    in a static UI.

## Why it's worth building

- **The reactive graph, not callbacks.** Nothing in `app.R` explicitly
  says "when the ledger changes, redraw the table." Every `render*()`
  block just reads `ledger()`, and Shiny's runtime tracks that read as a
  dependency and re-invokes the block whenever the value invalidates.
  That inversion — outputs *declare what they depend on* instead of
  producers *pushing to subscribers* — is the one idea the whole
  framework is built around.
- **Keeping business logic out of the reactive layer.** Every validation
  rule and aggregation lives in `R/ledger.R` as ordinary functions that
  take a data frame and return one. `app.R` never manipulates a data
  frame directly — it only calls those functions inside `observeEvent`/
  `render*`. That split is what makes `testthat` able to cover the real
  logic (17 tests) without a browser, a server process, or even loading
  the `shiny` package, while `testServer()` (see below) covers the thin
  reactive glue separately.

## Setup (Windows 11 / PowerShell)

```powershell
winget install --id RProject.R -e
# reopen PowerShell so R and Rscript are on PATH, then:
Rscript -e "install.packages(c('shiny', 'testthat'), repos = 'https://cloud.r-project.org')"
Rscript -e "R.version.string; packageVersion('shiny')"
```

## Run it

```
Rscript -e "shiny::runApp('.')"
```

Then open the printed `http://127.0.0.1:PORT` URL in a browser.

## Test it

```
cd tests
Rscript testthat.R
```

17 tests, all against `R/ledger.R` directly (no Shiny session needed).

The reactive wiring itself was checked with Shiny's own `testServer()`,
which drives the server function with simulated `session$setInputs()`
calls and inspects the reactive values in between — the add/add/remove
flow in this project's manual check (add income, add an expense, confirm
the balance, remove a row, confirm the row count) passed against the real
`server()` function, not a mock.

## Stretch goals (not implemented)

- Persist the ledger to a CSV or SQLite file across sessions with
  `shiny::onSessionEnded()` / a startup read.
- A `plotOutput()` bar chart of the category breakdown alongside the
  table (base `barplot()` is enough — no extra package needed).

## Hints

- `reactiveVal()` replaces its whole value on every update — there is no
  in-place `$<-` mutation. Every mutator in `R/ledger.R` returns a new
  data frame for exactly this reason; trying to `ledger()$foo <- x`
  simply doesn't work.
- `renderTable` silently drops a `data.frame` with zero rows into an
  empty table rather than erroring, which is why `summarize_by_category()`
  and `new_ledger()` return a correctly-typed zero-row frame (same
  columns, right types) instead of `NULL` — an output bound to `NULL`
  throws.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Tutorial: ["Build Web Apps with Shiny"](http://shiny.rstudio.com/tutorial/) by the RStudio/Shiny team.
