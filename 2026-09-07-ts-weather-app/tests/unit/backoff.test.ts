import { test, assert, assertEqual } from "./harness.js";
import { computeDelayMs, isRetryableStatus, type BackoffOptions } from "../../src/core/backoff.js";

const OPTS: BackoffOptions = { baseMs: 100, maxMs: 1000, maxAttempts: 5 };

test("computeDelayMs: with random()=0 the delay is always 0", () => {
  for (let attempt = 0; attempt < 5; attempt++) {
    assertEqual(computeDelayMs(attempt, OPTS, () => 0), 0);
  }
});

test("computeDelayMs: with random() just under 1, the delay approaches the per-attempt cap, capped at maxMs", () => {
  // attempt 0: cap = min(1000, 100*2^0) = 100
  // attempt 1: cap = min(1000, 100*2^1) = 200
  // attempt 5: cap = min(1000, 100*2^5) = 1000 (clamped by maxMs, not 3200)
  assertEqual(computeDelayMs(0, OPTS, () => 0.999), 99);
  assertEqual(computeDelayMs(1, OPTS, () => 0.999), 199);
  assertEqual(computeDelayMs(5, OPTS, () => 0.999), 999);
});

test("computeDelayMs: rejects a negative attempt", () => {
  let threw = false;
  try {
    computeDelayMs(-1, OPTS);
  } catch {
    threw = true;
  }
  assert(threw, "expected computeDelayMs(-1, ...) to throw");
});

test("isRetryableStatus: no status (network failure) is retryable", () => {
  assert(isRetryableStatus(undefined));
});

test("isRetryableStatus: 429 and 5xx are retryable", () => {
  assert(isRetryableStatus(429));
  assert(isRetryableStatus(500));
  assert(isRetryableStatus(503));
});

test("isRetryableStatus: other 4xx are not retryable", () => {
  assert(!isRetryableStatus(400));
  assert(!isRetryableStatus(404));
  assert(!isRetryableStatus(418));
});

test("isRetryableStatus: 2xx/3xx are not retryable (they're not failures)", () => {
  assert(!isRetryableStatus(200));
  assert(!isRetryableStatus(304));
});
