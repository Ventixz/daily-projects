# Learning: 2D Breakout in JavaScript

**Source:** ["2D Breakout game using Phaser"](https://developer.mozilla.org/en-US/docs/Games/Tutorials/2D_breakout_game_Phaser)
from the Game Development subsection of the JavaScript section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Built without Phaser: plain JavaScript, no dependencies (Node 18+ for tests).

## What it is

- `src/engine.js` — a pure game engine. `createGame()` returns a state object,
  `step(state)` advances one tick. No DOM, no timers, so it is fully testable.
- `src/ascii.js` — renders a state as text.
- `demo.js` — a tiny AI paddle plays a full game headlessly and prints frames.
- `index.html` — canvas front end (arrow keys); drives the same engine.
- `tests/engine.test.js` — 9 `node:test` tests.

## What I learned

- **Separate simulation from rendering.** Keeping state and `step()` pure let me
  test wall, ceiling, brick, paddle and life-loss behaviour with no browser.
- **Collision by region checks.** Ball-vs-brick is a point-in-rectangle test; one
  brick per tick, then flip `dy`.
- **Paddle bounce angle** comes from where the ball lands relative to the paddle
  centre, which is what makes the game controllable.
- **Game states** (`playing`/`won`/`lost`) and freezing `step()` once finished.
- Pitfall: `node --test tests/` (directory arg) failed on Node 22; plain `node --test` works.

## Known limits

The ball is a point for brick collisions (not a circle), and fast balls could
tunnel through bricks; fine at this speed.

## Run it

```bash
cd 2026-10-01-js-breakout
npm test       # 9 tests pass
npm run demo   # AI plays and wins
# or open index.html in a browser
```
