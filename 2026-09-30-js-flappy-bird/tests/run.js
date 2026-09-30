import assert from "node:assert/strict";
import { makeRng } from "../src/rng.js";
import { CONFIG, createGame, flap, tick, advance, restart, circleHitsRect } from "../src/game.js";

let passed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log("ok   " + name); }
  catch (e) { console.error("FAIL " + name + "\n" + e.stack); process.exitCode = 1; }
}
const run = (s, n) => { for (let i = 0; i < n; i++) tick(s); };

test("rng is deterministic per seed and in [0,1)", () => {
  const a = makeRng(42), b = makeRng(42);
  for (let i = 0; i < 100; i++) { const v = a(); assert.equal(v, b()); assert.ok(v >= 0 && v < 1); }
  assert.notEqual(makeRng(1)(), makeRng(2)());
});

test("ready phase: bird bobs but nothing else moves, no pipes spawn", () => {
  const s = createGame(1);
  run(s, 120);
  assert.equal(s.phase, "ready");
  assert.equal(s.pipes.length, 0);
  assert.ok(Math.abs(s.bird.y - (CONFIG.height / 2 - 30)) <= 6.01);
});

test("first flap starts the game and sets upward velocity", () => {
  const s = createGame(1);
  flap(s);
  assert.equal(s.phase, "playing");
  assert.equal(s.bird.vy, CONFIG.flapVelocity);
});

test("gravity pulls the bird down; fall speed is capped", () => {
  const s = createGame(1);
  s.phase = "playing";
  s.bird.y = 100;
  tick(s);
  assert.ok(s.bird.vy > 0);
  s.bird.vy = 10000;
  tick(s);
  assert.equal(s.bird.vy, CONFIG.maxFallSpeed);
});

test("flap moves bird up relative to freefall", () => {
  const a = createGame(1), b = createGame(1);
  for (const s of [a, b]) { s.phase = "playing"; s.bird.y = 200; }
  flap(b);
  run(a, 10); run(b, 10);
  assert.ok(b.bird.y < a.bird.y);
});

test("bird that never flaps hits the ground and dies", () => {
  const s = createGame(1);
  flap(s); s.bird.vy = 0;
  run(s, 300);
  assert.equal(s.phase, "dead");
  assert.equal(s.bird.y, CONFIG.height - CONFIG.groundHeight - CONFIG.birdRadius);
});

test("ceiling counts as a collision", () => {
  const s = createGame(1);
  s.phase = "playing"; s.bird.y = CONFIG.birdRadius + 1; s.bird.vy = -500;
  tick(s);
  assert.equal(s.phase, "dead");
});

test("pipes spawn with gaps fully inside the playable area", () => {
  const s = createGame(7);
  s.phase = "playing";
  for (let i = 0; i < 1200; i++) {
    s.bird.y = 200; s.bird.vy = 0; // pin bird so we only test spawning
    s.phase = "playing";
    tick(s);
  }
  assert.ok(s.pipes.length >= 2);
  for (const p of s.pipes) {
    assert.ok(p.gapTop >= CONFIG.minGapTop);
    assert.ok(p.gapTop + CONFIG.pipeGap <= CONFIG.height - CONFIG.groundHeight - CONFIG.minGapTop + 1e-9);
  }
});

test("pipe spacing is respected and offscreen pipes are removed", () => {
  const s = createGame(3);
  for (let i = 0; i < 2000; i++) { s.phase = "playing"; s.bird.y = 200; s.bird.vy = 0; tick(s); }
  for (let i = 1; i < s.pipes.length; i++) {
    assert.ok(Math.abs(s.pipes[i].x - s.pipes[i - 1].x - CONFIG.pipeSpacing) < 1e-6);
  }
  assert.ok(s.pipes.every((p) => p.x + CONFIG.pipeWidth > 0));
});

test("scoring: each pipe passed counts once", () => {
  const s = createGame(5);
  for (let i = 0; i < 1500; i++) {
    s.phase = "playing";
    // Autopilot: hold the bird at the centre of the next gap.
    const next = s.pipes.find((p) => p.x + CONFIG.pipeWidth >= CONFIG.birdX - CONFIG.birdRadius);
    s.bird.y = next ? next.gapTop + CONFIG.pipeGap / 2 : 200;
    s.bird.vy = 0;
    tick(s);
    assert.equal(s.phase, "playing", "autopilot should never collide (tick " + i + ")");
  }
  assert.ok(s.score >= 5, "score was " + s.score);
  assert.ok(s.pipes.every((p) => !p.scored || p.x + CONFIG.pipeWidth < CONFIG.birdX - CONFIG.birdRadius));
});

test("hitting a pipe wall kills the bird, even mid-gap horizontally", () => {
  const s = createGame(1);
  s.phase = "playing";
  s.pipes = [{ x: CONFIG.birdX - 10, gapTop: 100, scored: false }];
  s.bird.y = 50; s.bird.vy = 0; // above the gap: inside top pipe
  tick(s);
  assert.equal(s.phase, "dead");
});

test("passing through the gap is safe", () => {
  const s = createGame(1);
  s.phase = "playing";
  s.pipes = [{ x: CONFIG.birdX - 10, gapTop: 100, scored: false }];
  s.bird.y = 160; s.bird.vy = 0;
  tick(s);
  assert.equal(s.phase, "playing");
});

test("circleHitsRect: corner case uses true distance, not bounding box", () => {
  // circle centre diagonal from rect corner at distance ~ 14.14 > r=12
  assert.equal(circleHitsRect(110, 110, 12, 0, 0, 100, 100), false);
  assert.equal(circleHitsRect(105, 105, 12, 0, 0, 100, 100), true);
  assert.equal(circleHitsRect(50, 50, 1, 0, 0, 100, 100), true); // centre inside
});

test("dead bird ignores flaps and stays dead", () => {
  const s = createGame(1);
  s.phase = "dead"; s.bird.y = 100; s.bird.vy = 0;
  flap(s);
  assert.equal(s.phase, "dead");
  assert.equal(s.bird.vy, 0);
});

test("advance: fixed steps are frame-rate independent", () => {
  const mk = () => { const s = createGame(9); flap(s); return s; };
  const a = mk(), b = mk();
  for (let i = 0; i < 60; i++) advance(a, 1 / 60);   // 60 fps
  for (let i = 0; i < 30; i++) advance(b, 1 / 30);   // 30 fps
  assert.ok(Math.abs(a.bird.y - b.bird.y) < 1e-6 || a.phase !== b.phase);
  assert.ok(Math.abs(a.time - b.time) < 0.02);
});

test("advance: huge frame gaps are clamped", () => {
  const s = createGame(1);
  advance(s, 60);
  assert.ok(s.time <= 0.26);
});

test("same seed + same inputs => identical run (replayable)", () => {
  const play = () => {
    const s = createGame(123); flap(s);
    for (let i = 0; i < 600; i++) { if (i % 25 === 0) flap(s); tick(s); }
    return JSON.stringify({ b: s.bird, p: s.pipes, sc: s.score, ph: s.phase });
  };
  assert.equal(play(), play());
});

test("restart resets everything", () => {
  const s = createGame(1);
  flap(s); run(s, 200);
  restart(s, 2);
  assert.equal(s.phase, "ready");
  assert.equal(s.score, 0);
  assert.equal(s.pipes.length, 0);
});

console.log(`\n${passed} passed`);
