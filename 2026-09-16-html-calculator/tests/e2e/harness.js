// Same hand-rolled shape as tests/unit/harness.js, but async end-to-end
// against a real Chromium tab -- @playwright/test isn't installed (this
// project has zero npm dependencies of its own; it borrows the `playwright`
// package already present in this environment's global node_modules).
import { createRequire } from "node:module";
import { startServer } from "../../server.js";

// `require`, unlike `import`, still honors NODE_PATH, so that's the escape
// hatch used here to reach the globally-installed `playwright`.
const require = createRequire(import.meta.url);
const { chromium } = require("playwright");

const tests = [];

function test(name, fn) {
  tests.push({ name, fn });
}

function assert(condition, message) {
  if (!condition) throw new Error(message || "assertion failed");
}

function assertEqual(actual, expected, message) {
  const a = JSON.stringify(actual);
  const e = JSON.stringify(expected);
  if (a !== e) {
    throw new Error(`${message || "assertEqual failed"}\n  expected: ${e}\n  actual:   ${a}`);
  }
}

async function runAll() {
  const server = await startServer(0);
  const baseUrl = `http://127.0.0.1:${server.address().port}`;
  const browser = await chromium.launch();

  let pass = 0;
  let fail = 0;
  for (const { name, fn } of tests) {
    const context = await browser.newContext();
    const page = await context.newPage();
    try {
      await page.goto(baseUrl, { waitUntil: "load" });
      await fn(page);
      console.log(`ok   - ${name}`);
      pass++;
    } catch (err) {
      console.log(`FAIL - ${name}`);
      const detail = err && err.stack ? err.stack : String(err);
      console.log(
        detail
          .split("\n")
          .map((line) => `       ${line}`)
          .join("\n"),
      );
      fail++;
    } finally {
      await context.close();
    }
  }

  await browser.close();
  await new Promise((resolve) => server.close(resolve));

  console.log(`\n${pass} passed, ${fail} failed`);
  if (fail > 0) process.exitCode = 1;
}

export { test, assert, assertEqual, runAll };
