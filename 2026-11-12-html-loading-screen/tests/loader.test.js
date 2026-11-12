import test from "node:test";
import assert from "node:assert/strict";
import { runLoader, remainingVisibleMs, validateTasks, TimeoutError } from "../src/loader.js";

const flush = () => new Promise((r) => setImmediate(r));

// A clock whose time only moves when the test says so.
function fakeClock() {
  let now = 0;
  let nextId = 0;
  const timers = new Map();
  return {
    now: () => now,
    setTimeout(fn, ms) { const id = ++nextId; timers.set(id, { at: now + ms, fn }); return id; },
    clearTimeout(id) { timers.delete(id); },
    async advance(ms) {
      const end = now + ms;
      for (;;) {
        await flush();
        const due = [...timers.entries()]
          .filter(([, t]) => t.at <= end)
          .sort((a, b) => a[1].at - b[1].at || a[0] - b[0])[0];
        if (!due) break;
        timers.delete(due[0]);
        now = due[1].at;
        due[1].fn();
      }
      now = end;
      await flush();
    },
  };
}

const after = (clock, ms, value) => () => new Promise((r) => clock.setTimeout(() => r(value), ms));
const failAfter = (clock, ms, msg) => () => new Promise((_, rej) => clock.setTimeout(() => rej(new Error(msg)), ms));

function record(clock) {
  const events = [];
  return {
    events,
    onShow: () => events.push(["show", clock.now()]),
    onHide: () => events.push(["hide", clock.now()]),
    onProgress: (p) => events.push(["progress", clock.now(), p.fraction]),
  };
}

test("a fast load never shows the screen", async () => {
  const clock = fakeClock();
  const rec = record(clock);
  const run = runLoader([{ label: "a", weight: 1, run: after(clock, 100, 1) }], { ...rec, clock });
  await clock.advance(1000);
  const out = await run;
  assert.equal(out.shown, false);
  assert.deepEqual(rec.events.filter(([e]) => e !== "progress"), []);
});

test("once shown, the screen stays up for minVisibleMs", async () => {
  const clock = fakeClock();
  const rec = record(clock);
  const run = runLoader([{ label: "a", weight: 1, run: after(clock, 300) }], { ...rec, clock });
  await clock.advance(2000);
  assert.equal((await run).shown, true);
  assert.deepEqual(rec.events.filter(([e]) => e !== "progress"), [["show", 150], ["hide", 750]]);
});

test("a long load hides as soon as it finishes", async () => {
  const clock = fakeClock();
  const rec = record(clock);
  const run = runLoader([{ label: "a", weight: 1, run: after(clock, 1000) }], { ...rec, clock });
  await clock.advance(2000);
  await run;
  assert.deepEqual(rec.events.filter(([e]) => e === "hide"), [["hide", 1000]]);
});

test("progress is weighted, not counted", async () => {
  const clock = fakeClock();
  const rec = record(clock);
  const run = runLoader(
    [
      { label: "small", weight: 1, run: after(clock, 100) },
      { label: "big", weight: 3, run: after(clock, 200) },
    ],
    { ...rec, clock },
  );
  await clock.advance(1000);
  await run;
  const fractions = rec.events.filter(([e]) => e === "progress").map(([, , f]) => f);
  assert.deepEqual(fractions, [0, 0.25, 1]);
});

test("a failing task is reported and still counts toward progress", async () => {
  const clock = fakeClock();
  const rec = record(clock);
  const run = runLoader(
    [
      { label: "ok", weight: 1, run: after(clock, 50, "x") },
      { label: "bad", weight: 1, run: failAfter(clock, 80, "boom") },
    ],
    { ...rec, clock },
  );
  await clock.advance(500);
  const out = await run;
  assert.deepEqual(out.failures.map((f) => [f.label, f.error.message]), [["bad", "boom"]]);
  assert.equal(out.results[0].value, "x");
  assert.equal(rec.events.filter(([e]) => e === "progress").at(-1)[2], 1);
});

test("a synchronous throw inside run() is a failure, not a crash", async () => {
  const clock = fakeClock();
  const run = runLoader([{ label: "sync", weight: 1, run: () => { throw new Error("nope"); } }], { clock });
  await clock.advance(10);
  const out = await run;
  assert.equal(out.failures[0].error.message, "nope");
});

test("a hung task times out", async () => {
  const clock = fakeClock();
  const run = runLoader([{ label: "hang", weight: 1, run: () => new Promise(() => {}) }], { clock, taskTimeoutMs: 500 });
  await clock.advance(2000);
  const out = await run;
  assert.ok(out.failures[0].error instanceof TimeoutError);
  assert.match(out.failures[0].error.message, /hang timed out after 500ms/);
});

test("remainingVisibleMs", () => {
  assert.equal(remainingVisibleMs({ shownAt: null, finishedAt: 50, minVisibleMs: 600 }), 0);
  assert.equal(remainingVisibleMs({ shownAt: 150, finishedAt: 300, minVisibleMs: 600 }), 450);
  assert.equal(remainingVisibleMs({ shownAt: 150, finishedAt: 900, minVisibleMs: 600 }), 0);
});

test("validateTasks rejects bad input", () => {
  const run = () => {};
  assert.throws(() => validateTasks([]), /non-empty/);
  assert.throws(() => validateTasks([{ weight: 1, run }]), /label/);
  assert.throws(() => validateTasks([{ label: "a", weight: 0, run }]), /positive weight/);
  assert.throws(() => validateTasks([{ label: "a", weight: 1 }]), /run\(\)/);
  assert.throws(() => validateTasks([{ label: "a", weight: 1, run }, { label: "a", weight: 1, run }]), /duplicate/);
});
