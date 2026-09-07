// A TTL cache with one deliberate extra: `getStale` reads a value back even
// after it has expired. `get` is for "is this still fresh enough to skip a
// fetch"; `getStale` is for "the live fetch just failed, and something
// (even something old) beats a blank screen" -- see WeatherClient's offline
// fallback, which is the entire reason this cache has two read methods
// instead of one.

export interface CacheEntry<T> {
  value: T;
  expiresAt: number;
}

export class TtlCache<T> {
  private store = new Map<string, CacheEntry<T>>();

  constructor(
    private ttlMs: number,
    private now: () => number = Date.now,
  ) {}

  // Case and surrounding/interior whitespace shouldn't fragment the cache --
  // "Paris", " paris ", and "PARIS" are the same lookup.
  normalizeKey(raw: string): string {
    return raw.trim().toLowerCase().replace(/\s+/g, " ");
  }

  get(rawKey: string): T | undefined {
    const entry = this.store.get(this.normalizeKey(rawKey));
    if (!entry || entry.expiresAt <= this.now()) return undefined;
    return entry.value;
  }

  getStale(rawKey: string): T | undefined {
    return this.store.get(this.normalizeKey(rawKey))?.value;
  }

  set(rawKey: string, value: T): void {
    this.store.set(this.normalizeKey(rawKey), { value, expiresAt: this.now() + this.ttlMs });
  }

  has(rawKey: string): boolean {
    return this.get(rawKey) !== undefined;
  }
}
