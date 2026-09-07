// Hand-rolled test runner -- no test framework dependency, matching the
// other from-scratch test runners in this repo. A single module-level
// registry means every test file that imports `test` from here is
// registering into the same list, so run.ts just has to import the test
// files (for their side effect of calling `test(...)`) and then call
// runAll() once everything has registered.

interface Case {
  name: string;
  fn: () => void | Promise<void>;
}

const tests: Case[] = [];

export function test(name: string, fn: () => void | Promise<void>): void {
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

export async function assertRejects(
  promise: Promise<unknown>,
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  errorClass?: new (...args: any[]) => Error,
): Promise<void> {
  try {
    await promise;
  } catch (err) {
    if (errorClass && !(err instanceof errorClass)) {
      throw new Error(`expected rejection to be instanceof ${errorClass.name}, got ${String(err)}`);
    }
    return;
  }
  throw new Error("expected promise to reject, but it resolved");
}

export async function runAll(): Promise<void> {
  let pass = 0;
  let fail = 0;
  for (const { name, fn } of tests) {
    try {
      await fn();
      console.log(`ok   - ${name}`);
      pass++;
    } catch (err) {
      console.log(`FAIL - ${name}`);
      const detail = err instanceof Error && err.stack ? err.stack : String(err);
      console.log(
        detail
          .split("\n")
          .map((line) => `       ${line}`)
          .join("\n"),
      );
      fail++;
    }
  }
  console.log(`\n${pass} passed, ${fail} failed`);
  if (fail > 0) process.exitCode = 1;
}
