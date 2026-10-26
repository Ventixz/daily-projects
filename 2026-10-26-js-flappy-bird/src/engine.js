'use strict';
// Pure Flappy Bird engine: no DOM, deterministic given a seed.

const DEFAULTS = {
  width: 288, height: 400, birdX: 70, birdR: 10,
  gravity: 0.5, flapVel: -7, maxFall: 10,
  pipeW: 40, gap: 100, pipeSpeed: 2, pipeEvery: 90, margin: 40,
};

// Small deterministic PRNG (mulberry32) so tests and demos are repeatable.
function rng(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function createGame(opts = {}, seed = 1) {
  const o = { ...DEFAULTS, ...opts };
  return { o, rand: rng(seed), bird: { y: o.height / 2, vy: 0 }, pipes: [],
           tick: 0, score: 0, status: 'ready' };
}

function flap(g) {
  if (g.status === 'over') return;
  if (g.status === 'ready') g.status = 'playing';
  g.bird.vy = g.o.flapVel;
}

function spawnPipe(g) {
  const { o } = g;
  const gapTop = o.margin + g.rand() * (o.height - 2 * o.margin - o.gap);
  g.pipes.push({ x: o.width, gapTop, passed: false });
}

// Circle (bird) vs. the two rectangles of a pipe.
function hitsPipe(g, p) {
  const { o, bird } = g;
  const near = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  const rects = [[p.x, 0, p.x + o.pipeW, p.gapTop],
                 [p.x, p.gapTop + o.gap, p.x + o.pipeW, o.height]];
  return rects.some(([x1, y1, x2, y2]) => {
    const dx = o.birdX - near(o.birdX, x1, x2), dy = bird.y - near(bird.y, y1, y2);
    return dx * dx + dy * dy < o.birdR * o.birdR;
  });
}

function step(g) {
  if (g.status === 'over' || g.status === 'ready') return g;
  const { o, bird } = g;
  g.tick++;
  bird.vy = Math.min(bird.vy + o.gravity, o.maxFall);
  bird.y += bird.vy;

  if (g.tick % o.pipeEvery === 1) spawnPipe(g);
  for (const p of g.pipes) {
    p.x -= o.pipeSpeed;
    if (!p.passed && p.x + o.pipeW < o.birdX - o.birdR) { p.passed = true; g.score++; }
  }
  g.pipes = g.pipes.filter(p => p.x + o.pipeW > 0);

  if (bird.y - o.birdR < 0 || bird.y + o.birdR > o.height || g.pipes.some(p => hitsPipe(g, p))) {
    g.status = 'over';
  }
  return g;
}

module.exports = { DEFAULTS, rng, createGame, flap, step, hitsPipe };
