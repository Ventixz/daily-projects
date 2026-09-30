# Flappy Bird in vanilla JavaScript (JavaScript)

**Source:** "Make Flappy Bird in HTML5 and JavaScript with Phaser", from the
Game Development section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial leans on the Phaser engine for physics, collisions and the loop. I
kept the game but dropped the engine: everything Phaser hides (gravity
integration, collision math, the game loop, pipe spawning) is written by hand,
and split so that the game rules can be tested in plain Node with no browser.

## What it is

- `src/game.js` -- the whole game as pure functions over one state object:
  `createGame`, `flap`, `tick` (one fixed simulation step), `advance` (turns
  real elapsed time into fixed steps), `restart`. No DOM, no `Date.now()`, no
  `Math.random()`.
- `src/rng.js` -- seeded mulberry32 PRNG, so pipe layouts are reproducible.
- `src/render.js` -- state to canvas, one direction only.
- `app.js` -- input handling, `requestAnimationFrame` loop, best score in
  `localStorage`.
- `tests/run.js` -- 18 assertions, plain `node`, no dependencies.

## Run it

```bash
cd 2026-09-30-js-flappy-bird
make test    # 18 assertions
make serve   # http://localhost:8080  (Space or tap to flap)
```

## What it actually teaches

- **Fixed timestep vs. variable frame time.** `requestAnimationFrame` delivers
  frames at 60, 30 or irregular rates. If physics used the raw frame delta, the
  same flap would travel a different distance on different machines. `advance`
  accumulates elapsed time and runs `tick` in constant 1/60 s steps, keeping the
  remainder. The elapsed time is clamped to 0.25 s so a backgrounded tab does
  not come back and run thousands of catch-up steps. A test runs the same game
  at 60 fps and 30 fps and compares.
- **Determinism makes games testable.** Injecting the RNG (seeded) and never
  reading the clock inside `tick` means "same seed + same inputs = same run",
  which one test asserts by serializing two full runs and comparing. It also
  lets the scoring test use an autopilot that pins the bird to the gap centre
  and checks it never dies and scores 5+ pipes.
- **Semi-implicit Euler is enough for a flap.** Update velocity first, then
  position with the new velocity. A flap is just an instantaneous overwrite of
  `vy` (not an added impulse), which is why spamming the key can't launch the
  bird off-screen. Fall speed is capped so long drops stay controllable.
- **Circle-vs-rectangle collision is a clamp.** Clamp the bird's centre to the
  rectangle to find the nearest point, then compare squared distance to
  `r^2`. A bounding-box check would kill the bird near a pipe corner it
  visibly misses; one test puts the centre diagonal from a corner to prove it.
- **Pipes as a moving window.** New pipes spawn when the last one has moved
  `pipeSpacing` in from the right edge, and pipes are dropped once fully
  off-screen, so the list stays at 2-3 entries however long you play. Gap
  positions are drawn from the RNG within a margin so they are always
  reachable.
- **A phase enum beats scattered booleans.** `ready | playing | dead` decides
  what `tick` and `flap` do (idle bob, real physics, fall-and-stop), and a dead
  bird ignores flaps by construction.

## Deliberate scope cuts

- No sprites, sound or wing animation; shapes on a canvas.
- No headless browser test of `app.js`/`render.js`; only the game core is
  covered. I ran the server and checked it serves the page and modules, but
  I did not verify the rendering visually.
- Hard-coded 320x480 logical size; CSS scaling only.
- Scoring uses the pipe's trailing edge passing the bird's leading edge, which
  is slightly stricter than "centre passes centre".

## What I'd add next

- A Playwright test that flaps through the first pipe in a real page.
- Difficulty ramp (pipe speed and gap shrink with score).
- Input buffering, and recording inputs to replay a run from the seed.
