import { createGame, flap, advance, restart } from "./src/game.js";
import { render } from "./src/render.js";

const canvas = document.getElementById("game");
const ctx = canvas.getContext("2d");
const state = createGame(Date.now() & 0x7fffffff);
let best = 0;
try { best = Number(localStorage.getItem("flappy-best")) || 0; } catch {}

function input() {
  if (state.phase === "dead") restart(state);
  else flap(state);
}
window.addEventListener("keydown", (e) => {
  if (e.code === "Space") { e.preventDefault(); if (!e.repeat) input(); }
});
canvas.addEventListener("pointerdown", input);

let last = performance.now();
function frame(now) {
  advance(state, (now - last) / 1000);
  last = now;
  if (state.score > best) {
    best = state.score;
    try { localStorage.setItem("flappy-best", String(best)); } catch {}
  }
  render(ctx, state, best);
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
