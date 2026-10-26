'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { createGame, flap, step, hitsPipe, rng } = require('../src/engine');
const { render } = require('../src/ascii');

test('rng is deterministic and in [0,1)', () => {
  const a = rng(5), b = rng(5);
  for (let i = 0; i < 50; i++) { const v = a(); assert.strictEqual(v, b()); assert.ok(v >= 0 && v < 1); }
});

test('game waits in ready state until first flap', () => {
  const g = createGame();
  const y = g.bird.y;
  step(g); step(g);
  assert.strictEqual(g.status, 'ready');
  assert.strictEqual(g.bird.y, y);
  flap(g);
  assert.strictEqual(g.status, 'playing');
});

test('gravity pulls the bird down and flap pushes it up', () => {
  const g = createGame(); flap(g);
  const y0 = g.bird.y; step(g);
  assert.ok(g.bird.y < y0, 'flap moves up first');
  for (let i = 0; i < 16; i++) step(g);
  assert.ok(g.bird.vy > 0, 'gravity eventually reverses velocity');
});

test('fall speed is capped', () => {
  const g = createGame({ height: 100000 }); flap(g);
  for (let i = 0; i < 100; i++) step(g);
  assert.strictEqual(g.bird.vy, g.o.maxFall);
});

test('hitting the ground ends the game, then step is a no-op', () => {
  const g = createGame(); flap(g);
  while (g.status === 'playing') step(g);
  assert.strictEqual(g.status, 'over');
  const t = g.tick; step(g);
  assert.strictEqual(g.tick, t);
});

test('hitting the ceiling ends the game', () => {
  const g = createGame(); flap(g);
  g.bird.y = 5;
  step(g);
  assert.strictEqual(g.status, 'over');
});

test('pipe collision: gap is safe, wall is not', () => {
  const g = createGame();
  const p = { x: g.o.birdX - 5, gapTop: 150, passed: false };
  g.bird.y = 200; assert.strictEqual(hitsPipe(g, p), false);
  g.bird.y = 140; assert.strictEqual(hitsPipe(g, p), true);
  g.bird.y = 260; assert.strictEqual(hitsPipe(g, p), true);
});

test('pipes spawn on schedule with gaps inside margins', () => {
  const g = createGame({ height: 1e6, birdR: 0 }, 3); flap(g);
  const seen = [];
  for (let i = 0; i < 400; i++) { g.bird.y = 5e5; g.bird.vy = -g.o.gravity; step(g); g.pipes.forEach(p => seen.includes(p) || seen.push(p)); }
  assert.strictEqual(seen.length, Math.ceil(400 / g.o.pipeEvery));
  seen.forEach(p => assert.ok(p.gapTop >= g.o.margin && p.gapTop + g.o.gap <= g.o.height - g.o.margin));
});

test('passing a pipe scores exactly once; offscreen pipes are removed', () => {
  const g = createGame(); flap(g);
  g.pipes.push({ x: g.o.birdX - g.o.birdR - g.o.pipeW + 1, gapTop: 0, passed: false });
  g.pipes[0].gapTop = 150;
  g.bird.y = 200; g.bird.vy = -g.o.gravity; // hold altitude this tick
  step(g);
  assert.strictEqual(g.score, 1);
  g.bird.y = 200; g.bird.vy = -g.o.gravity; step(g);
  assert.strictEqual(g.score, 1);
  g.pipes[0].x = -g.o.pipeW; g.bird.y = 200; g.bird.vy = -g.o.gravity; step(g);
  assert.ok(!g.pipes.some(p => p.x + g.o.pipeW <= 0));
});

test('same seed gives the same pipes', () => {
  const run = () => { const g = createGame({}, 9); flap(g); for (let i = 0; i < 100; i++) { if (g.bird.y > 250) flap(g); step(g); } return g.pipes.map(p => p.gapTop); };
  assert.deepStrictEqual(run(), run());
});

test('ascii renderer shows bird and status', () => {
  const g = createGame();
  const s = render(g);
  assert.ok(s.includes('O') && s.includes('ready'));
});

test('autopilot demo scores at least one point', () => {
  const g = createGame({}, 7); flap(g);
  for (let t = 0; t < 5000 && g.status === 'playing'; t++) {
    const next = g.pipes.find(p => p.x + g.o.pipeW > g.o.birdX - g.o.birdR);
    const target = next ? next.gapTop + g.o.gap / 2 + 15 : g.o.height / 2;
    if (g.bird.y > target && g.bird.vy >= 0) flap(g);
    step(g);
  }
  assert.ok(g.score >= 1, `score ${g.score}`);
});
