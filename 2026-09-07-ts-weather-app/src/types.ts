export interface GeoResult {
  name: string;
  country: string;
  latitude: number;
  longitude: number;
}

// Temperature is Celsius; see units.ts for why display conversion is
// deferred to render time instead of stored here.
export interface WeatherSnapshot {
  city: string;
  country: string;
  temperatureC: number;
  windKph: number;
  condition: string;
  fetchedAt: number;
}

export interface WeatherResult extends WeatherSnapshot {
  // true when this snapshot came from the cache after a live fetch failed
  // (see WeatherClient.lookup's catch branch), not from a fresh response.
  stale: boolean;
}

// The seam between WeatherClient (retry/cache/race policy) and the actual
// transport. The real implementation (src/api/openMeteo.ts, written inline
// in app.ts) hits Open-Meteo; tests substitute a FakeWeatherApi that never
// touches the network.
export interface WeatherApi {
  geocode(query: string, signal: AbortSignal): Promise<GeoResult>;
  forecast(
    lat: number,
    lon: number,
    signal: AbortSignal,
  ): Promise<Pick<WeatherSnapshot, "temperatureC" | "windKph" | "condition">>;
}

export class HttpError extends Error {
  constructor(
    message: string,
    public status: number | undefined,
  ) {
    super(message);
    this.name = "HttpError";
  }
}

export class NotFoundError extends Error {
  constructor(public query: string) {
    super(`No location found for "${query}"`);
    this.name = "NotFoundError";
  }
}

// Thrown by WeatherClient.lookup only when a live fetch failed *and* there
// was no cached value (even a stale one) to fall back to.
export class LookupFailedError extends Error {
  constructor(public query: string, public cause: unknown) {
    super(`Could not fetch weather for "${query}"`);
    this.name = "LookupFailedError";
  }
}
