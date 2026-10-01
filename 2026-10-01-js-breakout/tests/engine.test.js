'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { createGame, step } = require('../src/engine');

test('creates rows*cols bricks, all alive', () => {
  const g = createGame({ rows: 2, cols: 4 });
  assert.strictEqual(g.bricks.length, 8);
  assert(g.bricks.every(b => b.alive));
});

test('ball bounces off side walls', () => {
  const g = createGame();
  g.ball.x = g.o.width - g.o.ballR - 1; g.ball.dx = 3; g.ball.dy = 0;
  step(g);
  assert(g.ball.dx < 0);
});

test('ball bounces off ceiling', () => {
  const g = createGame();
  g.ball.y = g.o.ballR + 1; g.ball.dy = -3; g.ball.dx = 0;
  step(g);
  assert(g.ball.dy > 0);
});

test('hitting a brick removes it, scores, and flips dy', () => {
  const g = createGame();
  const b = g.bricks[0];
  g.ball.x = b.x + 5; g.ball.y = b.y + 5; g.ball.dy = -3; g.ball.dx = 0;
  step(g);
  assert.strictEqual(b.alive, false);
  assert.strictEqual(g.score, 1);
  assert(g.ball.dy > 0);
});

test('paddle hit bounces up; edge hit angles the ball', () => {
  const g = createGame();
  g.ball.x = g.paddleX + g.o.paddleW - 1; g.ball.y = g.o.height - g.o.ballR; g.ball.dy = 3; g.ball.dx = 0;
  step(g);
  assert(g.ball.dy < 0);
  assert(g.ball.dx > 0);
});

test('missing the paddle costs a life and resets the ball', () => {
  const g = createGame();
  g.paddleX = 0; g.ball.x = g.o.width - 20; g.ball.y = g.o.height - g.o.ballR; g.ball.dy = 3;
  step(g);
  assert.strictEqual(g.lives, 2);
  assert.strictEqual(g.status, 'playing');
});

test('losing the last life ends the game and freezes state', () => {
  const g = createGame({ lives: 1 });
  g.paddleX = 0; g.ball.x = g.o.width - 20; g.ball.y = g.o.height - g.o.ballR; g.ball.dy = 3;
  step(g);
  assert.strictEqual(g.status, 'lost');
  const snap = JSON.stringify(g);
  step(g);
  assert.strictEqual(JSON.stringify(g), snap);
});

test('clearing the last brick wins', () => {
  const g = createGame({ rows: 1, cols: 1 });
  const b = g.bricks[0];
  g.ball.x = b.x + 5; g.ball.y = b.y + 5; g.ball.dy = -3;
  step(g);
  assert.strictEqual(g.status, 'won');
});

test('paddle is clamped to the field', () => {
  const g = createGame();
  g.input = -1; for (let i = 0; i < 200; i++) g.paddleX = Math.max(0, g.paddleX - 7);
  step(g);
  assert.strictEqual(g.paddleX, 0);
});
