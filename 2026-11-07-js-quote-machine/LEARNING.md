# Learning: Random Quote Machine (JavaScript)

**Source:** "Build A Random Quote Machine" in the JavaScript / React section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Scope: the tutorial is a video series built on React; this is my own dependency-free version: a testable core, a CLI, and a plain HTML page.

Run: `node cli.js 5 42` (5 quotes, seed 42) or open `index.html`. Test: `node --test`.

## Layout

- `quotes.js` — seedable PRNG (`mulberry32`), `QuoteMachine` (no immediate repeats), `format`, `tweetUrl`. Works in Node and the browser.
- `cli.js` — prints N quotes, optional seed.
- `index.html` — new-quote button and a tweet link.
- `quotes.test.js` — 7 `node:test` tests: determinism, no back-to-back repeats, full coverage, 1- and 2-quote edge cases, empty list, URL encoding.

## What I learned

- **"Random" that feels random isn't uniform random.** Pure `Math.random()` repeats a quote 1/n of the time, which looks like a bug. Picking from the other n-1 items and shifting the index past the last one gives a uniform no-repeat choice with no retry loop.
- **Inject the RNG.** Passing `rng` into the class lets tests use a seeded generator, so random behaviour becomes deterministic and assertable.
- **Keep logic out of the UI.** Separating the core from DOM code meant the Node tests covered everything that matters, and the HTML is just glue.
- **One file for Node and browsers** with a `typeof module` guard avoids needing a bundler for a tiny project.
- **`encodeURIComponent` is required for share links**; `&` in a quote would otherwise truncate the tweet text.

## Known limits

- Six hard-coded quotes; no fetching from an API as the tutorial does.
- No React, so none of its state or component lessons.
- The HTML page was not tested in a browser here, only the core logic.
