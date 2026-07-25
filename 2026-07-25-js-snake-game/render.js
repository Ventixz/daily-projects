import { createGame, setDirection, step } from "./game.js";

const CELL = 20;
const COLS = 20;
const ROWS = 20;
const TICK_MS = 110;

const canvas = document.getElementById("board");
canvas.width = COLS * CELL;
canvas.height = ROWS * CELL;
const ctx = canvas.getContext("2d");
const scoreEl = document.getElementById("score");

const KEY_DIRECTIONS = {
  ArrowUp: { x: 0, y: -1 },
  ArrowDown: { x: 0, y: 1 },
  ArrowLeft: { x: -1, y: 0 },
  ArrowRight: { x: 1, y: 0 },
  w: { x: 0, y: -1 },
  s: { x: 0, y: 1 },
  a: { x: -1, y: 0 },
  d: { x: 1, y: 0 },
};

let state = createGame({ cols: COLS, rows: ROWS });

document.addEventListener("keydown", (e) => {
  if (state.status === "over" && e.key === "Enter") {
    state = createGame({ cols: COLS, rows: ROWS });
    return;
  }
  const dir = KEY_DIRECTIONS[e.key];
  if (dir) {
    e.preventDefault();
    state = setDirection(state, dir);
  }
});

function draw() {
  ctx.fillStyle = "#111827";
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  ctx.fillStyle = "#22c55e";
  state.snake.forEach((seg, i) => {
    ctx.fillStyle = i === 0 ? "#4ade80" : "#22c55e";
    ctx.fillRect(seg.x * CELL + 1, seg.y * CELL + 1, CELL - 2, CELL - 2);
  });

  ctx.fillStyle = "#f87171";
  ctx.fillRect(state.food.x * CELL + 1, state.food.y * CELL + 1, CELL - 2, CELL - 2);

  scoreEl.textContent = `Score: ${state.score}`;

  if (state.status === "over") {
    ctx.fillStyle = "rgba(0, 0, 0, 0.6)";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = "#f9fafb";
    ctx.font = "20px monospace";
    ctx.textAlign = "center";
    ctx.fillText("Game over", canvas.width / 2, canvas.height / 2 - 12);
    ctx.font = "14px monospace";
    ctx.fillText("Press Enter to restart", canvas.width / 2, canvas.height / 2 + 12);
  }
}

let lastTick = 0;
function loop(timestamp) {
  if (timestamp - lastTick >= TICK_MS) {
    lastTick = timestamp;
    if (state.status === "playing") state = step(state);
    draw();
  }
  requestAnimationFrame(loop);
}

draw();
requestAnimationFrame(loop);
