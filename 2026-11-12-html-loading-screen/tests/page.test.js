// Browser tests: drive the real page in headless Chromium.
// Playwright isn't a dependency of this project; it's resolved via require(),
// which honours NODE_PATH (ESM imports don't), so point NODE_PATH at a global install.
import test, { before, after } from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { startServer } from "../server.js";

const { chromium } = createRequire(import.meta.url)("playwright");

let server, browser, base;

before(async () => {
  server = await startServer(0);
  base = `http://127.0.0.1:${server.address().port}/`;
  browser = await chromium.launch();
});

after(async () => {
  await browser?.close();
  server?.close();
});

// Record every data-state the overlay passes through, from the first script on.
const TRACK_STATES = () => {
  window.__states = [];
  new MutationObserver(() => {
    const el = document.getElementById("loader");
    if (el && window.__states.at(-1) !== el.dataset.state) window.__states.push(el.dataset.state);
  }).observe(document, { subtree: true, attributes: true, attributeFilter: ["data-state"] });
};

async function open(query = "", contextOptions = {}) {
  const context = await browser.newContext(contextOptions);
  const page = await context.newPage();
  await page.addInitScript(TRACK_STATES);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e));
  await page.goto(base + query);
  return { page, context, errors };
}

const loaded = (page) => page.waitForSelector("html[data-loaded=true]", { timeout: 10000 });

test("slow load shows the overlay, fills the bar, then reveals content", async () => {
  const { page, context, errors } = await open("?latency=800");
  await page.waitForSelector('#loader[data-state="visible"]');
  assert.equal(await page.locator("#content").evaluate((el) => getComputedStyle(el).visibility), "hidden");
  await loaded(page);
  assert.deepEqual(await page.evaluate(() => window.__states), ["visible", "leaving", "done"]);
  assert.equal(await page.getAttribute("#progress", "aria-valuenow"), "100");
  assert.equal(await page.getAttribute("#content", "aria-busy"), "false");
  assert.equal(await page.locator("#content-list li").count(), 4);
  assert.equal(await page.locator("#loader").isVisible(), false);
  assert.deepEqual(errors, []);
  await context.close();
});

test("fast load never shows the overlay", async () => {
  const { page, context } = await open("?latency=0");
  await loaded(page);
  assert.deepEqual(await page.evaluate(() => window.__states), ["done"]);
  assert.equal(await page.locator("#content-title").textContent(), "Loaded.");
  await context.close();
});

test("a failed task shows an error with a focused retry button", async () => {
  const { page, context } = await open("?latency=200&fail=content");
  await page.waitForSelector('#loader[data-state="error"]');
  assert.match(await page.textContent("#status"), /Couldn't load: Content/);
  assert.equal(await page.textContent("#loader-title"), "Something went wrong");
  assert.equal(await page.evaluate(() => document.activeElement.id), "retry");
  await page.click("#retry");
  await loaded(page);
  assert.equal(await page.locator("#retry").isHidden(), true);
  await context.close();
});

test("reduced motion stops the animations and still finishes", async () => {
  const { page, context } = await open("?latency=500", { reducedMotion: "reduce" });
  await page.waitForSelector('#loader[data-state="visible"]');
  const anim = await page.locator(".spinner__ring").evaluate((el) => getComputedStyle(el).animationName);
  assert.equal(anim, "none");
  await loaded(page); // no transitionend fires here; the fallback timer must resolve the fade
  await context.close();
});

test("without JavaScript the content is visible and the overlay is not", async () => {
  const context = await browser.newContext({ javaScriptEnabled: false });
  const page = await context.newPage();
  await page.goto(base);
  assert.equal(await page.locator("#content").evaluate((el) => getComputedStyle(el).visibility), "visible");
  assert.equal(await page.locator("#loader").isVisible(), false);
  await context.close();
});
