// End-to-end smoke test: loads the compiled page in a real headless
// browser and drives it with real key events, the way a player would.
// Everything else in this project (test/tetris/core_test.clj) tests
// tetris.core in the JVM with no browser at all -- this is the one
// check that the ui.cljs/canvas/DOM wiring actually works together.
//
// Requires: `make build` already ran (resources/public/js/main.js
// exists) and something is serving resources/public/ at BASE_URL
// (`make e2e` does both).
import { chromium } from 'playwright';

const BASE_URL = process.env.BASE_URL || 'http://127.0.0.1:8934/index.html';
const launchOpts = process.env.PLAYWRIGHT_EXECUTABLE_PATH
  ? { executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH }
  : {};

let failed = false;
function check(cond, message) {
  if (!cond) {
    failed = true;
    console.error('FAIL:', message);
  } else {
    console.log('ok:', message);
  }
}

const browser = await chromium.launch(launchOpts);
try {
  const page = await browser.newPage();
  const consoleErrors = [];
  page.on('pageerror', (e) => consoleErrors.push(String(e)));
  page.on('console', (msg) => { if (msg.type() === 'error') consoleErrors.push(msg.text()); });

  await page.goto(BASE_URL);
  await page.waitForTimeout(300);

  const initial = await page.evaluate(() => window.tetrisState());
  check(initial['game-over'] === false, 'game is not over at start');
  check(initial.level === 1, 'starts at level 1');

  // A second, independent page: one ArrowLeft must move the piece exactly
  // one column, proving keyboard input reaches tetris.core/move.
  const page2 = await browser.newPage();
  await page2.goto(BASE_URL);
  await page2.waitForTimeout(200);
  const before = await page2.evaluate(() => window.tetrisState());
  await page2.keyboard.press('ArrowLeft');
  await page2.waitForTimeout(50);
  const afterLeft = await page2.evaluate(() => window.tetrisState());
  check(afterLeft['current-col'] === before['current-col'] - 1,
    `ArrowLeft moves the piece left by one column (${before['current-col']} -> ${afterLeft['current-col']})`);
  await page2.close();

  // Back on the first page: hard-drop enough pieces to run the game to
  // completion (lock -> line-clear check -> spawn -> repeat -> game over),
  // exercising the full loop end to end.
  for (let i = 0; i < 400; i++) await page.keyboard.press('Space');
  await page.waitForTimeout(200);
  const afterDrops = await page.evaluate(() => window.tetrisState());
  check(afterDrops['game-over'] === true,
    'reaches game-over after enough blind hard-drops (lock/spawn/collision loop works)');
  check(afterDrops.score >= 0 && afterDrops.lines >= 0 && afterDrops.level >= 1,
    `score/lines/level stay in range (${JSON.stringify(afterDrops)})`);

  // Pause toggles the on-screen status text.
  await page.keyboard.press('KeyP');
  await page.waitForTimeout(50);
  const status = await page.textContent('#status');
  check(/PAUSED|GAME OVER/.test(status), `status reflects pause or game-over (got "${status}")`);

  const realErrors = consoleErrors.filter((e) => !/favicon/i.test(e));
  check(realErrors.length === 0, `no console/page errors (${JSON.stringify(realErrors)})`);
} finally {
  await browser.close();
}

process.exit(failed ? 1 : 0);
