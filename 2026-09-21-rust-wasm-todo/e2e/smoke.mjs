// Drives the compiled page in real headless Chromium. The Rust unit tests cover
// state.rs in isolation; this is the one check that dom.rs's event wiring,
// set_inner_html rendering, hash routing, and localStorage persistence actually
// work together in a browser, which cargo test structurally cannot see.
import { chromium } from "playwright";
import assert from "node:assert/strict";

const URL = process.env.BASE_URL ?? "http://127.0.0.1:8877/index.html";
const launchOpts = process.env.PLAYWRIGHT_EXECUTABLE_PATH
  ? { executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH }
  : {};

const browser = await chromium.launch(launchOpts);
try {
  const page = await browser.newPage();
  const errors = [];
  page.on("pageerror", (e) => errors.push(String(e)));

  await page.goto(URL);
  await page.waitForSelector("#new-todo");

  // Add two todos.
  await page.fill("#new-todo", "write the BRIEF");
  await page.press("#new-todo", "Enter");
  await page.fill("#new-todo", "  ");
  await page.press("#new-todo", "Enter"); // blank: must not add a row
  await page.fill("#new-todo", "ship the PR");
  await page.press("#new-todo", "Enter");

  let items = await page.$$eval("#list li .text", (els) => els.map((e) => e.textContent));
  assert.deepEqual(items, ["write the BRIEF", "ship the PR"], "blank entry must be rejected");

  let count = await page.textContent("#count");
  assert.equal(count.trim(), "2 items left");

  // `location.hash` updates synchronously on click, but the `hashchange` event
  // (and thus our re-render) fires on a later task -- waiting on the hash alone
  // races the render. Wait for the render's own output (the selected filter
  // link) instead, so the assertion below always sees the settled DOM.

  // Toggle the first one done, filter to Active, confirm it drops out.
  await page.click("#list li:first-child .toggle");
  await page.click('a[href="#/active"]');
  await page.waitForFunction(
    () => document.querySelector(".filters a.selected")?.getAttribute("href") === "#/active"
  );
  items = await page.$$eval("#list li .text", (els) => els.map((e) => e.textContent));
  assert.deepEqual(items, ["ship the PR"], "completed todo should be hidden under Active filter");

  // Completed filter shows only the done one.
  await page.click('a[href="#/completed"]');
  await page.waitForFunction(
    () => document.querySelector(".filters a.selected")?.getAttribute("href") === "#/completed"
  );
  items = await page.$$eval("#list li .text", (els) => els.map((e) => e.textContent));
  assert.deepEqual(items, ["write the BRIEF"]);

  // Back to All, then reload: localStorage persistence must survive a fresh page load.
  await page.click('a[href="#/"]');
  await page.waitForFunction(
    () => document.querySelector(".filters a.selected")?.getAttribute("href") === "#/"
  );
  await page.reload();
  await page.waitForSelector("#new-todo");
  items = await page.$$eval("#list li .text", (els) => els.map((e) => e.textContent));
  assert.deepEqual(items, ["write the BRIEF", "ship the PR"], "todos must survive a reload");

  // Remove one, clear completed removes the other.
  await page.click("#clear-completed");
  items = await page.$$eval("#list li .text", (els) => els.map((e) => e.textContent));
  assert.deepEqual(items, ["ship the PR"]);

  // A todo containing markup must render as text, not run as script.
  await page.fill("#new-todo", "<img src=x onerror=window.__xss=1>");
  await page.press("#new-todo", "Enter");
  const xssRan = await page.evaluate(() => window.__xss === 1);
  assert.equal(xssRan, false, "todo text must be HTML-escaped, not injected");

  assert.deepEqual(errors, [], `page threw: ${errors.join("; ")}`);

  console.log("smoke: ok");
} finally {
  await browser.close();
}
