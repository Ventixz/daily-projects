// Exponential backoff with *full* jitter (the AWS Architecture Blog's
// "Exponential Backoff And Jitter" formula: delay = random(0, min(cap,
// base * 2^attempt))), not the more obvious base*2^attempt with no
// randomness. Plain exponential backoff synchronizes retries: if a burst of
// requests all fail at once (the API restarts, say), they all wait exactly
// backoff(1) and then all retry at exactly the same instant, reproducing the
// overload that failed them the first time. Full jitter spreads that retry
// burst across the whole window instead of hitting it as a wall.
//
// `random` is a parameter (defaulting to Math.random) specifically so tests
// can pin it to a fixed sequence and assert exact delays, rather than only
// asserting "delay is somewhere in range" -- see backoff.test.ts.

export interface BackoffOptions {
  baseMs: number;
  maxMs: number;
  maxAttempts: number;
}

export const DEFAULT_BACKOFF: BackoffOptions = {
  baseMs: 200,
  maxMs: 5000,
  maxAttempts: 4,
};

export function computeDelayMs(
  attempt: number,
  options: BackoffOptions,
  random: () => number = Math.random,
): number {
  if (attempt < 0) throw new Error("attempt must be >= 0");
  const cap = Math.min(options.maxMs, options.baseMs * 2 ** attempt);
  return Math.floor(random() * cap);
}

// A missing status means the request never got a response at all (DNS
// failure, connection reset, abort) -- treated as transient, same as 5xx.
// 429 (rate limited) is also transient. Any other 4xx means the request
// itself was malformed or the resource doesn't exist; retrying it verbatim
// will just fail the same way every time, so it isn't retryable.
export function isRetryableStatus(status: number | undefined): boolean {
  if (status === undefined) return true;
  if (status === 429) return true;
  return status >= 500;
}
