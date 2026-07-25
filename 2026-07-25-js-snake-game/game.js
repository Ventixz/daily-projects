// Pure game-state logic for Snake. No DOM, no timers, no globals — every
// function takes a state and returns a new state, so it can run identically
// in a browser event loop or a Node test file.

export function createGame({ cols = 20, rows = 20, rng = Math.random } = {}) {
  const startY = Math.floor(rows / 2);
  const startX = Math.floor(cols / 2);
  const snake = [
    { x: startX, y: startY },
    { x: startX - 1, y: startY },
    { x: startX - 2, y: startY },
  ];
  return {
    cols,
    rows,
    snake,
    direction: { x: 1, y: 0 },
    food: placeFood(cols, rows, snake, rng),
    score: 0,
    status: "playing",
  };
}

// Rejects the 180-degree reversal (running the snake into its own neck) and
// any non-cardinal vector. Any other requested direction is accepted, even
// if it won't take effect until the next tick.
export function setDirection(state, dir) {
  const isCardinal =
    (Math.abs(dir.x) === 1 && dir.y === 0) ||
    (Math.abs(dir.y) === 1 && dir.x === 0);
  if (!isCardinal) return state;

  const isReversal =
    dir.x === -state.direction.x && dir.y === -state.direction.y;
  if (isReversal) return state;

  return { ...state, direction: dir };
}

export function step(state, rng = Math.random) {
  if (state.status !== "playing") return state;

  const head = state.snake[0];
  const newHead = { x: head.x + state.direction.x, y: head.y + state.direction.y };

  const hitWall =
    newHead.x < 0 || newHead.x >= state.cols || newHead.y < 0 || newHead.y >= state.rows;
  if (hitWall) return { ...state, status: "over" };

  // The tail cell is excluded: it vacates as the head moves in, so it's a
  // legal destination (this is what lets the snake follow its own tail).
  const body = state.snake.slice(0, -1);
  const hitSelf = body.some((seg) => seg.x === newHead.x && seg.y === newHead.y);
  if (hitSelf) return { ...state, status: "over" };

  const ate = newHead.x === state.food.x && newHead.y === state.food.y;
  const newSnake = [newHead, ...state.snake];
  if (!ate) newSnake.pop();

  return {
    ...state,
    snake: newSnake,
    food: ate ? placeFood(state.cols, state.rows, newSnake, rng) : state.food,
    score: ate ? state.score + 1 : state.score,
    status: "playing",
  };
}

// Rejection sampling: pick random cells until one isn't under the snake.
// Fine for a 20x20 board; would need a free-list swap for boards dense
// enough that most draws collide.
export function placeFood(cols, rows, snake, rng = Math.random) {
  let cell;
  do {
    cell = { x: Math.floor(rng() * cols), y: Math.floor(rng() * rows) };
  } while (snake.some((seg) => seg.x === cell.x && seg.y === cell.y));
  return cell;
}
