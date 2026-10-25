# Learning: Movie Voting Core in JavaScript

**Source:** "Build a Full Stack Movie Voting App with Test-First Development using Mocha, React, Redux and Immutable"
in the JavaScript → Web Applications section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
I built the tutorial's first part, the application core (the tournament logic), with plain Node and no dependencies,
using deep-frozen objects and `node:test` in place of Immutable and Mocha.

Run: `node cli.js "A,B,C" "B,B,A" "C"` (entries, then one comma-separated vote list per round). Test: `npm test`.

## Layout

- `core.js` — `setEntries`, `next`, `vote` and a Redux-style `reducer`, all pure functions over frozen state.
- `core.test.js` — 8 tests: immutability, pairing, tallying, winners, ties, final winner, reducer.
- `cli.js` — scripted demo of a whole tournament.

## What I learned

- **Design the state shape first.** `{entries, vote: {pair, tally}, winner}` makes every transition a small function.
- **Pure functions plus frozen data are easy to test.** No setup or mocks; call a function, compare the result.
  Deep freezing makes accidental mutation throw instead of silently corrupting state.
- **A reducer is just `(state, action) => state`.** Wrapping the core functions in one switch gives a Redux-shaped
  API for free, which is what the later parts of the tutorial plug into.
- **Edge cases fall out of the tests.** Ties return both entries to the queue; votes for entries not in the
  current pair are ignored; the last remaining entry becomes the winner.

## Known limits

- Only the core is built; the tutorial's server, socket and React parts are not.
- Votes are not tied to a voter, so one voter can vote repeatedly.
- `deepFreeze` is a development aid and is not free on large states.
