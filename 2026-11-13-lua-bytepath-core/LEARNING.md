# Learning: A BYTEPATH-style game engine core in Lua

**Source:** "BYTEPATH: Creation of a Complete Game with Lua and LÖVE" by a327ex (Parts 1–6: Game Loop, Libraries,
Rooms and Areas, Game Basics, Player Basics), from the Lua / LÖVE section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The full series is 16 parts and builds a whole game, so this covers the **engine core** the early parts set up.
The tutorial (GitHub issues on `a327ex/blog`) was blocked from the build environment, so the code follows the
series' outline rather than its text. The libraries it pulls in (classic, hump.timer, boipushy) are replaced by
small versions I wrote, which is where most of the learning is.

> Written by the daily-project routine (Claude). Commits are authored as `Claude`, so they are separate
> from the repo owner's own work in the history.

Run headless (plain Lua 5.1–5.4 or LuaJIT, no LÖVE needed): `lua headless.lua` prints ASCII snapshots of a scripted
flight (steer, boost, brake, fly off the edge, die, restart). `lua headless.lua --quiet` prints just the summary.
Run with graphics: `love .` (LÖVE 11.x). Arrow keys or WASD; up boosts, down brakes; Esc quits.
Test: `lua tests/run.lua` (19 tests, no dependencies).

## Layout

- `lib/class.lua`: single-inheritance classes: `extend`, `super`, `is`, calling a class constructs an instance.
- `lib/timer.lua`: `after`, `every` (with count), `tween` (4 easings), tags that replace or cancel timers.
- `lib/input.lua`: keys bound to named actions; `pressed` / `released` (one frame) and `down` (held).
- `engine/gameobject.lua`: base object: position, id, depth, `dead` flag, its own timer.
- `engine/area.lua`: updates objects, removes dead ones, draw order by depth, `queryCircleArea`, `getClosestObject`.
- `engine/rooms.lua`: room manager: `gotoRoom`, deferred `request`, rebuilt vs `persistent` rooms.
- `engine/loop.lua`: fixed-timestep accumulator with interpolation alpha and a clamp on long frames.
- `game/`: `Player` (rotate, accelerate, boost/brake, auto-fire, dies off-screen), `Projectile`, `Stage` room.
- `main.lua` + `conf.lua`: the LÖVE front end. `headless.lua`: the scripted text runner.

## What I learned

- **A game loop is update-then-draw, but the `dt` you get is not the `dt` you want.** Real frame times wobble.
  Feeding them straight into physics makes results depend on frame rate. The accumulator in `engine/loop.lua`
  steps the simulation in equal 1/60 s slices whatever the frame time, so the headless run (with deliberately
  jittery frame times) is fully deterministic. A test runs it twice and checks the results match exactly.
- **Clamp the frame time or you get a spiral of death.** After a 5-second stall, an unclamped loop would try to run
  300 steps at once. That makes the next frame slow too, and the loop never catches up. Capping at 0.25 s trades
  a short slow-motion blip for staying responsive.
- **Float sums need an epsilon.** Adding 1/60 sixty times doesn't land exactly on 1.0, so a strict `>=` check
  sometimes skips a step. The loop test covers this case.
- **A timer library replaces most hand-written cooldown counters.** `every(0.24, shoot)` is the whole fire-rate
  system. Tags make it safe: starting `"restart"` twice replaces the first one instead of stacking, and
  `cancel("shoot")` turns a behaviour off.
- **Never mutate a list while iterating it.** Timer callbacks add and cancel timers, and objects spawn objects
  while the Area is updating. The timer iterates over a snapshot. The Area walks backwards from the length it
  had at the start, so `table.remove` doesn't skip items and newborn objects first update next frame. Both cases
  have tests.
- **Kill objects with a flag; let the owner remove them.** Objects set `dead = true` and the Area removes them at a
  safe point, then calls `destroy()` to clear timers and drop the back-reference to the area. Otherwise a pending
  timer could fire on an object that is no longer in the game.
- **Rooms make restarting free.** `gotoRoom("Stage")` builds a brand-new Stage and destroys the old one, so there is
  no reset code to keep in sync with the init code. A menu that should remember its state sets
  `persistent = true` and is reused instead. Switching rooms in the middle of the current room's update is risky,
  so `request()` defers the switch until that update has finished.
- **Bind actions, not keys.** Game code asks `input:down("left")`, so arrows and WASD both work and rebinding never
  touches game logic. `pressed` must only be true on the first frame: OS key-repeat sends more `keypressed` events,
  so `Input` ignores presses for keys that are already held.
- **"Always moving, steer by rotating" movement is three lines.** Turn by `rv * dt`, then `v = min(v + a*dt, max_v)`,
  then move along `(cos r, sin r)`. Boost and brake only change `max_v`. Because of the `min`, braking drops the
  speed straight away while boosting builds it up gradually.
- **Screen y points down**, so positive rotation turns clockwise and "up" is `-pi/2`. The ASCII arrows in
  `renderAscii` had to account for this.
- **Keep the logic free of the engine.** Nothing outside `main.lua` touches `love.*`. Draw functions take a
  graphics object as an argument. That's why the whole game runs and is tested under plain `lua`, and why the
  same code runs under LuaJIT (LÖVE's runtime): no `table.pack`, and `goto` is a reserved word, hence `gotoRoom`.

## Known limits

- I never ran `main.lua` in a real LÖVE window, because LÖVE isn't available here. It was smoke-tested with a stubbed
  `love` table under Lua 5.4 and LuaJIT: load, 120 frames of update/draw, key events.
- Drawing ignores the loop's interpolation `alpha`, so motion can stutter slightly when the monitor refresh rate
  doesn't match 60 Hz.
- No camera, screen shake, collision with enemies, enemies, ammo or HP. Those come in the later BYTEPATH parts.
- Tweens only handle flat numeric fields (no nested tables), and there are only four easing functions.
