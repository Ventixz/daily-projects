// Loader orchestration with no DOM access, so it can be unit-tested in Node.
//
// Three ideas live here:
//   1. Weighted progress: each task has a weight, and progress is the share of
//      total weight that has settled (succeeded or failed).
//   2. No flash: the loading screen is only shown if loading is still going
//      after `showDelayMs`. Fast loads never see it.
//   3. No flicker: once shown, it stays up at least `minVisibleMs`, so it never
//      blinks on and off.

export const defaultClock = {
  now: () => (typeof performance !== "undefined" ? performance.now() : Date.now()),
  setTimeout: (fn, ms) => setTimeout(fn, ms),
  clearTimeout: (id) => clearTimeout(id),
};

export function totalWeight(tasks) {
  return tasks.reduce((sum, t) => sum + t.weight, 0);
}

// How long to keep the screen up after work finished, given when it appeared.
export function remainingVisibleMs({ shownAt, finishedAt, minVisibleMs }) {
  if (shownAt === null || shownAt === undefined) return 0;
  return Math.max(0, shownAt + minVisibleMs - finishedAt);
}

export function validateTasks(tasks) {
  if (!Array.isArray(tasks) || tasks.length === 0) {
    throw new TypeError("tasks must be a non-empty array");
  }
  const labels = new Set();
  for (const t of tasks) {
    if (typeof t?.label !== "string" || t.label === "") throw new TypeError("every task needs a label");
    if (labels.has(t.label)) throw new TypeError(`duplicate task label: ${t.label}`);
    labels.add(t.label);
    if (typeof t.run !== "function") throw new TypeError(`task ${t.label} needs a run() function`);
    if (!(Number.isFinite(t.weight) && t.weight > 0)) throw new TypeError(`task ${t.label} needs a positive weight`);
  }
}

export class TimeoutError extends Error {
  constructor(label, ms) {
    super(`${label} timed out after ${ms}ms`);
    this.name = "TimeoutError";
  }
}

function withTimeout(promise, ms, label, clock) {
  if (!ms) return promise;
  let id;
  const timeout = new Promise((_, reject) => {
    id = clock.setTimeout(() => reject(new TimeoutError(label, ms)), ms);
  });
  return Promise.race([promise, timeout]).finally(() => clock.clearTimeout(id));
}

export async function runLoader(tasks, options = {}) {
  validateTasks(tasks);
  const {
    onProgress = () => {},
    onShow = () => {},
    onHide = () => {},
    showDelayMs = 150,
    minVisibleMs = 600,
    taskTimeoutMs = 0,
    clock = defaultClock,
  } = options;

  const total = totalWeight(tasks);
  let settledWeight = 0;
  let shownAt = null;

  const showTimer = clock.setTimeout(() => {
    shownAt = clock.now();
    onShow();
  }, showDelayMs);

  onProgress({ fraction: 0, label: null, ok: null });

  const results = await Promise.all(
    tasks.map(async (task) => {
      let result;
      try {
        // Promise.resolve().then() also turns a synchronous throw into a rejection.
        const value = await withTimeout(Promise.resolve().then(task.run), taskTimeoutMs, task.label, clock);
        result = { label: task.label, ok: true, value };
      } catch (error) {
        result = { label: task.label, ok: false, error };
      }
      settledWeight += task.weight;
      onProgress({ fraction: settledWeight / total, label: task.label, ok: result.ok });
      return result;
    }),
  );

  clock.clearTimeout(showTimer);
  const shown = shownAt !== null;
  if (shown) {
    const wait = remainingVisibleMs({ shownAt, finishedAt: clock.now(), minVisibleMs });
    if (wait > 0) await new Promise((resolve) => clock.setTimeout(resolve, wait));
    onHide();
  }

  return { shown, results, failures: results.filter((r) => !r.ok) };
}
