// Same hand-rolled shape as tests/unit/harness.ts, but async end-to-end
// against a real Chromium tab -- @playwright/test isn't installed (this
// project has zero npm dependencies of its own; it borrows the `playwright`
// package already present in this environment's global node_modules), so
// this is the small part of that test runner this project actually needs.
import { createRequire } from "node:module";
import { startServer } from "../../server.js";

const require = createRequire(import.meta.url);
const { chromium } = require("playwright");

// `playwright` is intentionally not a listed dependency of this project
// (see package.json), so its types aren't resolvable at compile time
// either -- `Page` here is a structural stand-in covering only the methods
// these tests actually call, not the real (much larger) Playwright type.
export interface PageLike {
  goto(url: string): Promise<unknown>;
  fill(selector: string, value: string): Promise<void>;
  click(selector: string): Promise<void>;
  textContent(selector: string): Promise<string | null>;
  getAttribute(selector: string, name: string): Promise<string | null>;
  locator(selector: string): { getAttribute(name: string): Promise<string | null> };
  waitForSelector(selector: string, options?: { timeout?: number }): Promise<unknown>;
  waitForFunction(fn: () => unknown, options?: { timeout?: number }): Promise<unknown>;
  waitForTimeout(ms: number): Promise<void>;
}

export interface TestContext {
  page: PageLike;
  appBaseUrl: string;
}

interface Case {
  name: string;
  fn: (ctx: TestContext) => Promise<void>;
}

const tests: Case[] = [];

export function test(name: string, fn: (ctx: TestContext) => Promise<void>): void {
  tests.push({ name, fn });
}

export function assert(condition: unknown, message?: string): asserts condition {
  if (!condition) throw new Error(message ?? "assertion failed");
}

export function assertEqual(actual: unknown, expected: unknown, message?: string): void {
  const a = JSON.stringify(actual);
  const e = JSON.stringify(expected);
  if (a !== e) {
    throw new Error(`${message ?? "assertEqual failed"}\n  expected: ${e}\n  actual:   ${a}`);
  }
}

export async function runAll(): Promise<void> {
  const server = await startServer(0);
  const address = server.address();
  const port = typeof address === "object" && address ? address.port : 0;
  const appBaseUrl = `http://127.0.0.1:${port}`;
  const browser = await chromium.launch();

  let pass = 0;
  let fail = 0;
  for (const { name, fn } of tests) {
    const context = await browser.newContext();
    const page: PageLike = await context.newPage();
    try {
      await fn({ page, appBaseUrl });
      console.log(`ok   - ${name}`);
      pass++;
    } catch (err) {
      console.log(`FAIL - ${name}`);
      const detail = err instanceof Error && err.stack ? err.stack : String(err);
      console.log(
        detail
          .split("\n")
          .map((line: string) => `       ${line}`)
          .join("\n"),
      );
      fail++;
    } finally {
      await context.close();
    }
  }

  await browser.close();
  await new Promise<void>((resolve) => server.close(() => resolve()));

  console.log(`\n${pass} passed, ${fail} failed`);
  if (fail > 0) process.exitCode = 1;
}
