'use strict';
// Headless demo: a simple autopilot aims for the centre of the next gap.
const { createGame, flap, step } = require('./src/engine');
const { render } = require('./src/ascii');
const g = createGame({}, 7);
flap(g);
let t = 0;
while (g.status === 'playing' && t < 5000) {
  const next = g.pipes.find(p => p.x + g.o.pipeW > g.o.birdX - g.o.birdR);
  const target = next ? next.gapTop + g.o.gap / 2 + 15 : g.o.height / 2;
  if (g.bird.y > target && g.bird.vy >= 0) flap(g);
  step(g);
  if (t % 500 === 0) console.log(render(g) + '\n');
  t++;
}
console.log(render(g));
console.log(`finished after ${t} ticks, score ${g.score}`);
