'use strict';
// Render a game state as ASCII (20 px per cell) for the headless demo.
function render(g, cell = 10) {
  const { o } = g, cols = Math.ceil(o.width / cell), rows = Math.ceil(o.height / cell);
  const grid = Array.from({ length: rows }, () => Array(cols).fill(' '));
  for (const p of g.pipes) {
    for (let r = 0; r < rows; r++) {
      const y = r * cell;
      if (y < p.gapTop - cell / 2 || y > p.gapTop + o.gap - cell / 2) {
        for (let c = Math.max(0, Math.floor(p.x / cell)); c <= Math.min(cols - 1, Math.floor((p.x + o.pipeW) / cell)); c++) grid[r][c] = '#';
      }
    }
  }
  const br = Math.min(rows - 1, Math.max(0, Math.floor(g.bird.y / cell)));
  grid[br][Math.floor(o.birdX / cell)] = 'O';
  return grid.map(r => r.join('')).join('\n') + `\nscore ${g.score}  ${g.status}`;
}
module.exports = { render };
