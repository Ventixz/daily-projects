# Learning: Random Quote Machine (JavaScript)

**Source:** "Build A Random Quote Machine" (video series) in the JavaScript / React section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Scope: a dependency-free version (no React): a tested quote-dealing core plus a static HTML page.

Run: open `index.html` in a browser. Test: `node --test` (Node 18+).

## Layout

- `quotes.js` — quote data, seedable `mulberry32` PRNG, Fisher-Yates `shuffle`, `QuoteMachine` (deck dealing), `tweetUrl`.
- `index.html` — the UI: quote, author, "New quote" button, tweet link.
- `quotes.test.js` — 7 tests: shuffle purity and determinism, full-cycle coverage, no back-to-back repeats, edge cases, URL encoding.

## What I learned

- **`Math.random()` per click repeats quotes.** Dealing from a shuffled deck guarantees each quote appears once per cycle, which feels more random to a user.
- **Reshuffling creates a boundary bug:** the last card of one cycle can be the first of the next. The machine swaps it away; a 50-seed test checks it.
- **Fisher-Yates is the correct shuffle;** sorting with a random comparator is biased.
- **Injecting the RNG (a seeded mulberry32) makes randomness testable,** the same trick as seeding in the genetic-algorithm project.
- **Keep logic out of the DOM.** The core is a plain module that works in Node tests and the browser via a `module` guard, so the page is only wiring.
- Use `textContent` and `encodeURIComponent` rather than `innerHTML` and string concatenation for anything quote-derived.

## Known limits

- Eight hard-coded quotes; the original tutorial fetches from an API.
- No React, no animation, and the DOM wiring is not covered by tests.
