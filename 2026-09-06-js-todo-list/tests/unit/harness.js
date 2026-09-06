// Hand-rolled test runner -- no test framework dependency, matching the
// other from-scratch test runners in this repo (Java, C#, Rust). A single
// module-level registry means every test file that imports `test` from here
// is registering into the same list, so run.js just has to import the test
// files (for their side effect of calling `test(...)`) and then call
// runAll() once everything has registered.

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

function assertThrows(fn, message) {
  try {
    fn();
  } catch {
    return;
  }
  throw new Error(message || "expected function to throw, but it did not");
}

async function runAll() {
  let pass = 0;
  let fail = 0;
  for (const { name, fn } of tests) {
    try {
      await fn();
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
    }
  }
  console.log(`\n${pass} passed, ${fail} failed`);
  if (fail > 0) process.exitCode = 1;
}

export { test, assert, assertEqual, assertThrows, runAll };
