# Snake (JavaScript)

**Source:** [Build Snake using only JavaScript, HTML & CSS](https://www.freecodecamp.org/news/think-like-a-programmer-how-to-build-snake-using-only-javascript-html-and-css-7b1479c3339e/),
from [practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

## What it is

`game.js` is the whole game as pure functions: `createGame`, `setDirection`, `step`, and
`placeFood` all take a state object and return a new one — no DOM, no timers, no `Math.random()`
called directly (it's an injectable argument). `render.js` is the only file that touches the
`<canvas>`, the keyboard, or `requestAnimationFrame`; it just calls into `game.js` on a fixed
tick and draws whatever state comes back. `tests/game.test.js` runs the whole rules engine
(movement, wall/self collision, growth, food placement) under Node's built-in test runner —
no npm install needed, same as the rest of this repo's projects.

## Run it

```bash
cd 2026-07-25-js-snake-game
python3 -m http.server 8000      # or any static file server
# open http://localhost:8000 in a browser — arrow keys / WASD to move, Enter to restart

npm test                          # 9 tests, no dependencies to install
```

## What it actually teaches

- **Separating game state from rendering makes the rules testable without a browser.** Every
  interesting bug in Snake — does the snake die running into its own neck? can it legally chase
  its own tail? does food ever spawn under the snake? — lives in `step` and `placeFood`, neither
  of which imports `document` or `canvas`. That's what let `tests/game.test.js` assert on exact
  collision geometry (a hooked 5-segment snake driving into `segment[3]`) without spinning up
  `jsdom` or a headless browser. `render.js` only has one job left: draw a state object 60 times
  a second and turn keystrokes into `setDirection` calls.
- **The tail is legal ground, and that's not a special case — it falls out of the collision
  check.** `step` compares the new head only against `state.snake.slice(0, -1)`, excluding the
  tail. That's not a workaround bolted on for the "chase your own tail" rule; it's true because
  the tail cell is always vacated on the same tick the head moves in (unless the snake is
  growing, in which case the head can't land there anyway — food is never placed on the snake).
  One `slice(0, -1)` replaces what would otherwise be an explicit "am I about to eat my own
  tail, in which case actually that's fine" branch.
- **Rejecting the 180-degree reversal has to happen at input time, not render time.** The classic
  Snake bug is pressing the opposite arrow key and immediately dying because the head spun
  around into its own neck before the next frame even drew. `setDirection` checks the requested
  vector against `state.direction` (not against whatever key was pressed last) and simply
  refuses the reversal — the stored direction doesn't change, so the next `step` keeps going the
  way it was already going. Validating in the state-update function instead of the keydown
  handler means the same rule applies no matter what ever calls `setDirection`.
- **Decoupling game speed from frame rate needs a timestamp accumulator, not `setInterval`.**
  `requestAnimationFrame` fires at the display's refresh rate (usually 60Hz), way faster than
  Snake should move. `render.js` tracks `lastTick` and only calls `step` once
  `timestamp - lastTick >= TICK_MS` (110ms), but still redraws and requests the next frame every
  time. That keeps rendering smooth and input-responsive while the actual game logic — the part
  that has to look like discrete grid moves — advances on its own clock.
- **Rejection sampling is the right amount of cleverness for placing food on a small board.**
  `placeFood` just draws random cells until one isn't under the snake. On a 20x20 board with a
  snake that starts at 3 cells, the odds of a collision are tiny, so the "wasteful" retry loop
  costs nothing in practice. A free-list (tracking every empty cell explicitly) would only start
  paying for itself once the snake covers a large fraction of the board — not worth the
  complexity here.

## What I'd add next (stretch goals I skipped for scope)

- A speed ramp — shrinking `TICK_MS` slightly every N points eaten — since right now the game
  never gets harder, just longer.
- Wrapping (or an explicit toggle for it) instead of hard walls, to see how one boolean changes
  the wall-collision branch in `step` into a modulo on `newHead`.
- Persisting a high score across reloads via `localStorage`, which is the one place this project
  would touch a real browser API from outside `render.js`'s existing DOM boundary.
