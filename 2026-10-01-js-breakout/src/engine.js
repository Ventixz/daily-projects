'use strict';
// Pure Breakout engine: no DOM, no timers. step(state) advances one tick.

function createGame(opts = {}) {
  const o = { width: 480, height: 320, rows: 3, cols: 5, brickW: 75, brickH: 20,
    brickPad: 10, offsetTop: 30, offsetLeft: 30, paddleW: 75, paddleH: 10,
    ballR: 10, speed: 3, lives: 3, ...opts };
  const bricks = [];
  for (let r = 0; r < o.rows; r++)
    for (let c = 0; c < o.cols; c++)
      bricks.push({
        x: c * (o.brickW + o.brickPad) + o.offsetLeft,
        y: r * (o.brickH + o.brickPad) + o.offsetTop,
        alive: true,
      });
  const s = { o, bricks, score: 0, lives: o.lives, status: 'playing', input: 0 };
  resetBall(s);
  return s;
}

function resetBall(s) {
  const { o } = s;
  s.ball = { x: o.width / 2, y: o.height - 30, dx: o.speed, dy: -o.speed };
  s.paddleX = (o.width - o.paddleW) / 2;
}

function step(s) {
  if (s.status !== 'playing') return s;
  const { o, ball } = s;

  // paddle: input is -1, 0, 1
  s.paddleX = Math.max(0, Math.min(o.width - o.paddleW, s.paddleX + s.input * 7));

  // walls and ceiling
  if (ball.x + ball.dx > o.width - o.ballR || ball.x + ball.dx < o.ballR) ball.dx = -ball.dx;
  if (ball.y + ball.dy < o.ballR) ball.dy = -ball.dy;
  else if (ball.y + ball.dy > o.height - o.ballR) {
    if (ball.x > s.paddleX && ball.x < s.paddleX + o.paddleW) {
      // bounce angle depends on where the paddle was hit
      const hit = (ball.x - (s.paddleX + o.paddleW / 2)) / (o.paddleW / 2);
      ball.dx = hit * o.speed * 1.5;
      ball.dy = -Math.abs(ball.dy);
    } else {
      s.lives--;
      if (s.lives <= 0) { s.status = 'lost'; return s; }
      resetBall(s);
      return s;
    }
  }

  // bricks: hit at most one per tick, flip dy
  for (const b of s.bricks) {
    if (b.alive && ball.x > b.x && ball.x < b.x + o.brickW &&
        ball.y > b.y && ball.y < b.y + o.brickH) {
      b.alive = false;
      ball.dy = -ball.dy;
      s.score++;
      if (s.bricks.every(k => !k.alive)) s.status = 'won';
      break;
    }
  }

  ball.x += ball.dx;
  ball.y += ball.dy;
  return s;
}

module.exports = { createGame, step, resetBall };
