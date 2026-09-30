export function render(ctx, state, best) {
  const c = state.config;
  ctx.fillStyle = "#70c5ce";
  ctx.fillRect(0, 0, c.width, c.height);

  ctx.fillStyle = "#4caf50";
  for (const p of state.pipes) {
    const bottomY = p.gapTop + c.pipeGap;
    ctx.fillRect(p.x, 0, c.pipeWidth, p.gapTop);
    ctx.fillRect(p.x, bottomY, c.pipeWidth, c.height - bottomY);
  }

  ctx.fillStyle = "#ded895";
  ctx.fillRect(0, c.height - c.groundHeight, c.width, c.groundHeight);

  // Tilt the bird with its vertical velocity.
  ctx.save();
  ctx.translate(c.birdX, state.bird.y);
  ctx.rotate(Math.max(-0.5, Math.min(1.2, state.bird.vy / 400)));
  ctx.fillStyle = "#ffd54f";
  ctx.beginPath();
  ctx.arc(0, 0, c.birdRadius, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = "#000";
  ctx.fillRect(4, -5, 3, 3);
  ctx.restore();

  ctx.fillStyle = "#fff";
  ctx.strokeStyle = "#000";
  ctx.lineWidth = 3;
  ctx.font = "bold 36px sans-serif";
  ctx.textAlign = "center";
  ctx.strokeText(String(state.score), c.width / 2, 60);
  ctx.fillText(String(state.score), c.width / 2, 60);

  ctx.font = "bold 20px sans-serif";
  if (state.phase === "ready") ctx.fillText("Press Space / tap", c.width / 2, c.height / 2 + 60);
  if (state.phase === "dead") {
    ctx.fillText("Game over - best " + best, c.width / 2, c.height / 2);
    ctx.fillText("Press Space / tap to retry", c.width / 2, c.height / 2 + 30);
  }
}
