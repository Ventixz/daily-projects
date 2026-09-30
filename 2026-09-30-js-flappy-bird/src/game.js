import { makeRng } from "./rng.js";

export const CONFIG = {
  width: 320,
  height: 480,
  groundHeight: 60,
  birdX: 80,
  birdRadius: 12,
  gravity: 1200, // px/s^2
  flapVelocity: -330, // px/s (negative = up)
  maxFallSpeed: 500,
  pipeWidth: 52,
  pipeGap: 120,
  pipeSpacing: 180, // horizontal distance between pipe leading edges
  pipeSpeed: 120, // px/s
  minGapTop: 60,
  step: 1 / 60, // fixed simulation step, seconds
};

export function createGame(seed = 1, config = CONFIG) {
  return {
    config,
    rng: makeRng(seed),
    phase: "ready", // ready | playing | dead
    bird: { y: config.height / 2 - 30, vy: 0 },
    pipes: [],
    score: 0,
    time: 0,
    accumulator: 0,
  };
}

function spawnPipe(state, x) {
  const c = state.config;
  const playable = c.height - c.groundHeight;
  const lo = c.minGapTop;
  const hi = playable - c.minGapTop - c.pipeGap;
  const gapTop = lo + state.rng() * (hi - lo);
  state.pipes.push({ x, gapTop, scored: false });
}

export function flap(state) {
  if (state.phase === "ready") state.phase = "playing";
  if (state.phase === "playing") state.bird.vy = state.config.flapVelocity;
}

export function restart(state, seed) {
  Object.assign(state, createGame(seed ?? Math.floor(state.rng() * 2 ** 31), state.config));
}

// Circle vs. rectangle: clamp the circle centre to the rect, compare distance.
export function circleHitsRect(cx, cy, r, rx, ry, rw, rh) {
  const nx = Math.max(rx, Math.min(cx, rx + rw));
  const ny = Math.max(ry, Math.min(cy, ry + rh));
  const dx = cx - nx;
  const dy = cy - ny;
  return dx * dx + dy * dy < r * r;
}

function collides(state) {
  const c = state.config;
  const { y } = state.bird;
  if (y + c.birdRadius >= c.height - c.groundHeight) return true;
  if (y - c.birdRadius <= 0) return true;
  for (const p of state.pipes) {
    if (circleHitsRect(c.birdX, y, c.birdRadius, p.x, 0, c.pipeWidth, p.gapTop)) return true;
    const bottomY = p.gapTop + c.pipeGap;
    if (circleHitsRect(c.birdX, y, c.birdRadius, p.x, bottomY, c.pipeWidth, c.height - bottomY)) return true;
  }
  return false;
}

// One fixed simulation step. Semi-implicit Euler: update velocity, then position.
export function tick(state, dt = state.config.step) {
  const c = state.config;
  state.time += dt;
  if (state.phase === "ready") {
    state.bird.y = c.height / 2 - 30 + Math.sin(state.time * 4) * 6; // idle bob
    return;
  }
  if (state.phase === "dead") {
    // Fall to the ground and stop.
    state.bird.vy = Math.min(state.bird.vy + c.gravity * dt, c.maxFallSpeed);
    state.bird.y = Math.min(state.bird.y + state.bird.vy * dt, c.height - c.groundHeight - c.birdRadius);
    return;
  }

  state.bird.vy = Math.min(state.bird.vy + c.gravity * dt, c.maxFallSpeed);
  state.bird.y += state.bird.vy * dt;

  for (const p of state.pipes) p.x -= c.pipeSpeed * dt;
  state.pipes = state.pipes.filter((p) => p.x + c.pipeWidth > 0);

  const last = state.pipes[state.pipes.length - 1];
  if (!last || last.x <= c.width - c.pipeSpacing) spawnPipe(state, last ? last.x + c.pipeSpacing : c.width + 40);

  for (const p of state.pipes) {
    if (!p.scored && p.x + c.pipeWidth < c.birdX - c.birdRadius) {
      p.scored = true;
      state.score += 1;
    }
  }

  if (collides(state)) state.phase = "dead";
}

// Real frames vary in length; the simulation must not. Feed elapsed wall time
// in, run as many fixed steps as fit, keep the remainder. Clamp to avoid the
// "spiral of death" after a backgrounded tab.
export function advance(state, elapsed) {
  const c = state.config;
  state.accumulator += Math.min(elapsed, 0.25);
  while (state.accumulator >= c.step) {
    tick(state, c.step);
    state.accumulator -= c.step;
  }
}
