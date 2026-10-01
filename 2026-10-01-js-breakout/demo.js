'use strict';
// Headless demo: a simple AI tracks the ball; prints a few frames.
const { createGame, step } = require('./src/engine');
const { render } = require('./src/ascii');
const g = createGame();
let t = 0;
while (g.status === 'playing' && t < 20000) {
  const mid = g.paddleX + g.o.paddleW / 2;
  g.input = g.ball.x > mid + 5 ? 1 : g.ball.x < mid - 5 ? -1 : 0;
  step(g);
  if (t % 400 === 0) console.log(render(g) + '\n');
  t++;
}
console.log(render(g));
console.log(`finished after ${t} ticks`);
