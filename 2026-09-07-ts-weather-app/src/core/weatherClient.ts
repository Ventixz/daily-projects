import { TtlCache } from "./cache.js";
import { computeDelayMs, isRetryableStatus, DEFAULT_BACKOFF, type BackoffOptions } from "./backoff.js";
import { HttpError, NotFoundError, LookupFailedError } from "../types.js";
import type { WeatherApi, WeatherSnapshot, WeatherResult } from "../types.js";

export interface WeatherClientOptions {
  ttlMs?: number;
  backoff?: BackoffOptions;
  now?: () => number;
  random?: () => number;
  sleep?: (ms: number) => Promise<void>;
}

const realSleep = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

// Orchestrates a weather lookup on top of a raw WeatherApi: retries
// transient failures with backoff, de-dupes identical concurrent queries
// onto one network round trip, cancels a superseded query's request, and
// falls back to a stale cached value rather than an empty screen when a
// live fetch ultimately fails. None of that policy lives in the UI layer --
// render.ts only ever sees a WeatherResult, an error, or null.
export class WeatherClient {
  private cache: TtlCache<WeatherSnapshot>;
  private inFlight = new Map<string, Promise<WeatherSnapshot>>();
  // The normalized key of the most recently *started* lookup() call --
  // deliberately not a monotonic request counter. A counter would also
  // supersede two concurrent calls for the *same* city (the second call
  // bumping the counter past the first's id), which breaks deduping: both
  // calls share one network round trip, so both must be allowed to resolve
  // with the real result, not just whichever happened to start last.
  private latestKey: string | null = null;
  private activeController: AbortController | null = null;
  private backoff: BackoffOptions;
  private random: () => number;
  private sleep: (ms: number) => Promise<void>;

  constructor(
    private api: WeatherApi,
    options: WeatherClientOptions = {},
  ) {
    this.cache = new TtlCache<WeatherSnapshot>(options.ttlMs ?? 10 * 60 * 1000, options.now ?? Date.now);
    this.backoff = options.backoff ?? DEFAULT_BACKOFF;
    this.random = options.random ?? Math.random;
    this.sleep = options.sleep ?? realSleep;
  }

  // Resolves to:
  //  - a WeatherResult (stale: false) on a fresh cache hit or successful fetch
  //  - a WeatherResult (stale: true) if the fetch failed but a previous
  //    result for this query is still in the cache, even expired
  //  - null if a lookup() for a *different* city has started since this one
  //    began -- the caller should discard the result rather than render it,
  //    because whatever that newer search finds is what belongs on screen
  //    now, and this one lost the race
  // and throws NotFoundError or LookupFailedError otherwise.
  async lookup(query: string): Promise<WeatherResult | null> {
    const key = this.cache.normalizeKey(query);
    this.latestKey = key;

    const fresh = this.cache.get(query);
    if (fresh) return { ...fresh, stale: false };

    let promise = this.inFlight.get(key);
    if (!promise) {
      // Only cancel the previous request when this one isn't just going to
      // reuse it -- deduping and cancellation are separate concerns that
      // happen to share the "is this key already in flight" check.
      if (this.activeController) this.activeController.abort();
      const controller = new AbortController();
      this.activeController = controller;
      promise = this.fetchWithRetry(query, controller.signal).finally(() => {
        this.inFlight.delete(key);
      });
      this.inFlight.set(key, promise);
    }

    let snapshot: WeatherSnapshot;
    let stale = false;
    try {
      snapshot = await promise;
    } catch (err) {
      if (err instanceof NotFoundError) throw err;
      const cached = this.cache.getStale(query);
      if (!cached) throw new LookupFailedError(query, err);
      snapshot = cached;
      stale = true;
    }

    if (this.latestKey !== key) return null;
    return { ...snapshot, stale };
  }

  private async fetchWithRetry(query: string, signal: AbortSignal): Promise<WeatherSnapshot> {
    let lastError: unknown = new Error(`fetchWithRetry: ${query} never attempted`);
    for (let attempt = 0; attempt < this.backoff.maxAttempts; attempt++) {
      if (signal.aborted) throw lastError;
      try {
        const geo = await this.api.geocode(query, signal);
        const forecast = await this.api.forecast(geo.latitude, geo.longitude, signal);
        const snapshot: WeatherSnapshot = {
          city: geo.name,
          country: geo.country,
          fetchedAt: Date.now(),
          ...forecast,
        };
        this.cache.set(query, snapshot);
        return snapshot;
      } catch (err) {
        if (err instanceof NotFoundError) throw err;
        if (signal.aborted) throw err;

        lastError = err;
        const status = err instanceof HttpError ? err.status : undefined;
        if (!isRetryableStatus(status)) throw err;

        const isLastAttempt = attempt === this.backoff.maxAttempts - 1;
        if (!isLastAttempt) {
          await this.sleep(computeDelayMs(attempt, this.backoff, this.random));
        }
      }
    }
    throw lastError;
  }
}
