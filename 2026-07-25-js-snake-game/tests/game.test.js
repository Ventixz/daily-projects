import test from "node:test";
import assert from "node:assert/strict";
import { createGame, setDirection, step, placeFood } from "../game.js";

// Deterministic rng: replays a fixed sequence of [0,1) values, then repeats
// the last one. Lets tests pin down exactly where placeFood lands.
function sequenceRng(values) {
  let i = 0;
  return () => values[Math.min(i++, values.length - 1)];
}

test("createGame starts with a 3-segment snake moving right and food off the snake", () => {
  const state = createGame({ cols: 10, rows: 10, rng: sequenceRng([0.9, 0.9]) });
  assert.equal(state.snake.length, 3);
  assert.deepEqual(state.direction, { x: 1, y: 0 });
  assert.equal(state.status, "playing");
  assert.equal(state.score, 0);
  const onSnake = state.snake.some((seg) => seg.x === state.food.x && seg.y === state.food.y);
  assert.equal(onSnake, false);
});

test("setDirection rejects a 180-degree reversal but allows a turn", () => {
  const state = createGame({ cols: 10, rows: 10 });
  const reversed = setDirection(state, { x: -1, y: 0 });
  assert.deepEqual(reversed.direction, { x: 1, y: 0 }, "reversal must be ignored");

  const turned = setDirection(state, { x: 0, y: 1 });
  assert.deepEqual(turned.direction, { x: 0, y: 1 });
});

test("setDirection rejects non-cardinal input", () => {
  const state = createGame({ cols: 10, rows: 10 });
  const result = setDirection(state, { x: 1, y: 1 });
  assert.deepEqual(result.direction, { x: 1, y: 0 });
});

test("step moves the snake one cell without growing when it doesn't eat", () => {
  const state = {
    cols: 10,
    rows: 10,
    snake: [{ x: 5, y: 5 }, { x: 4, y: 5 }, { x: 3, y: 5 }],
    direction: { x: 1, y: 0 },
    food: { x: 0, y: 0 },
    score: 0,
    status: "playing",
  };
  const next = step(state);
  assert.deepEqual(next.snake, [{ x: 6, y: 5 }, { x: 5, y: 5 }, { x: 4, y: 5 }]);
  assert.equal(next.score, 0);
  assert.equal(next.status, "playing");
});

test("step ends the game when the snake runs into a wall", () => {
  const state = {
    cols: 10,
    rows: 10,
    snake: [{ x: 9, y: 5 }, { x: 8, y: 5 }],
    direction: { x: 1, y: 0 },
    food: { x: 0, y: 0 },
    score: 0,
    status: "playing",
  };
  const next = step(state);
  assert.equal(next.status, "over");
});

test("step ends the game when the snake runs into its own body", () => {
  // A hooked 5-segment snake: moving right drives the head into segment[3],
  // a real body cell (not the tail, which is excluded from the check).
  const state = {
    cols: 10,
    rows: 10,
    snake: [
      { x: 5, y: 5 },
      { x: 5, y: 6 },
      { x: 6, y: 6 },
      { x: 6, y: 5 },
      { x: 6, y: 4 },
    ],
    direction: { x: 1, y: 0 },
    food: { x: 0, y: 0 },
    score: 0,
    status: "playing",
  };
  const next = step(state);
  assert.equal(next.status, "over");
});

test("step following its own tail is legal, not a collision", () => {
  // A closed 4-cell loop: moving down sends the head onto the tail's current
  // cell, which is legal because the tail vacates it on the same tick.
  const state = {
    cols: 10,
    rows: 10,
    snake: [
      { x: 5, y: 5 },
      { x: 6, y: 5 },
      { x: 6, y: 6 },
      { x: 5, y: 6 },
    ],
    direction: { x: 0, y: 1 },
    food: { x: 0, y: 0 },
    score: 0,
    status: "playing",
  };
  const next = step(state);
  assert.equal(next.status, "playing");
  assert.deepEqual(next.snake[0], { x: 5, y: 6 });
});

test("step grows the snake, increments score, and moves the food when it eats", () => {
  const state = {
    cols: 10,
    rows: 10,
    snake: [{ x: 5, y: 5 }, { x: 4, y: 5 }, { x: 3, y: 5 }],
    direction: { x: 1, y: 0 },
    food: { x: 6, y: 5 },
    score: 2,
    status: "playing",
  };
  const rng = sequenceRng([0.1, 0.1]);
  const next = step(state, rng);
  assert.equal(next.snake.length, 4, "snake should grow by one segment");
  assert.deepEqual(next.snake[0], { x: 6, y: 5 });
  assert.equal(next.score, 3);
  const onSnake = next.snake.some((seg) => seg.x === next.food.x && seg.y === next.food.y);
  assert.equal(onSnake, false, "new food must not spawn on the snake");
});

test("placeFood never lands on an occupied cell, even when the first draws collide", () => {
  const snake = [{ x: 0, y: 0 }, { x: 1, y: 0 }];
  // First two draws land back on (0,0), which is occupied; third draw is free.
  const rng = sequenceRng([0, 0, 0, 0, 0.6, 0.6]);
  const food = placeFood(2, 2, snake, rng);
  const onSnake = snake.some((seg) => seg.x === food.x && seg.y === food.y);
  assert.equal(onSnake, false);
});
