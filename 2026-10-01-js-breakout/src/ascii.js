'use strict';
// Render a state as text, 10 px per character cell.
function render(s, k = 10) {
  const { o } = s;
  const w = Math.ceil(o.width / k), h = Math.ceil(o.height / k);
  const g = Array.from({ length: h }, () => Array(w).fill(' '));
  const put = (x, y, ch) => { const c = Math.floor(x / k), r = Math.floor(y / k);
    if (g[r] && c >= 0 && c < w) g[r][c] = ch; };
  for (const b of s.bricks) if (b.alive)
    for (let x = b.x; x < b.x + o.brickW; x += k) put(x, b.y, '#');
  for (let x = s.paddleX; x < s.paddleX + o.paddleW; x += k) put(x, o.height - 5, '=');
  put(s.ball.x, s.ball.y, 'o');
  return g.map(r => '|' + r.join('') + '|').join('\n') +
    `\nscore ${s.score}  lives ${s.lives}  ${s.status}`;
}
module.exports = { render };
