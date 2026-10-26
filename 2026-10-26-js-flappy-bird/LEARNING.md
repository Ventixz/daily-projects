# Learning: Flappy Bird in JavaScript

**Source:** "Make Flappy Bird in HTML5 and JavaScript with Phaser" in the JavaScript → Game Development section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Built with plain Canvas and no Phaser, so the physics and collision logic are written by hand.

Run: open `index.html` in a browser (Space / click to flap). Headless demo: `node demo.js`. Test: `node --test`.

## Layout

- `src/engine.js` — pure game logic: seeded PRNG, `createGame`, `flap`, `step`, circle-vs-rectangle `hitsPipe`.
- `src/ascii.js` — ASCII renderer used by the demo.
- `index.html` — canvas front end (input + drawing only).
- `demo.js` — autopilot that plays the game headlessly.
- `tests/engine.test.js` — 12 tests: determinism, ready state, gravity/flap, terminal-velocity cap, ground and ceiling,
  pipe collisions, spawn schedule and gap margins, one-time scoring, offscreen cleanup, autopilot.

## What I learned

- **Flappy Bird is just gravity plus an impulse.** Each tick adds a constant to velocity and velocity to position;
  a flap *sets* velocity (not adds), which makes every flap feel identical.
- **Separate logic from rendering.** Keeping the engine free of the DOM meant the whole game could be unit-tested and
  played by a script with `node --test`.
- **Circle vs. rectangle collision** is a clamp: find the closest point on the rectangle to the circle's centre and
  compare the squared distance to `r²`. A pipe is two rectangles around the gap.
- **Seeded randomness makes games testable.** A small PRNG (mulberry32) gives reproducible pipe heights.
- **Fixed-timestep updates** (one `step` per frame) keep physics deterministic; frame-rate-independent timing would need
  an accumulator.

## Known limits

- Physics is tied to the frame rate (60 Hz assumed), so a 144 Hz display plays faster.
- No sprites, sound, or high-score persistence.
- The autopilot is a simple heuristic, not an optimal player.
