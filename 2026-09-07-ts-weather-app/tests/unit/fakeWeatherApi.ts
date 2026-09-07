import type { WeatherApi, GeoResult, WeatherSnapshot } from "../../src/types.js";

// A WeatherApi whose geocode/forecast behavior per call is entirely up to
// the test: `geocodeImpl`/`forecastImpl` receive a zero-based call counter,
// so a test can script "fail twice, then succeed" or similar sequences
// without a separate mocking library.
export class ScriptedWeatherApi implements WeatherApi {
  geocodeCallCount = 0;
  forecastCallCount = 0;

  constructor(
    private geocodeImpl: (query: string, signal: AbortSignal, call: number) => Promise<GeoResult>,
    private forecastImpl: (
      lat: number,
      lon: number,
      signal: AbortSignal,
      call: number,
    ) => Promise<Pick<WeatherSnapshot, "temperatureC" | "windKph" | "condition">> = async () => DEFAULT_FORECAST,
  ) {}

  geocode(query: string, signal: AbortSignal): Promise<GeoResult> {
    return this.geocodeImpl(query, signal, this.geocodeCallCount++);
  }

  forecast(
    lat: number,
    lon: number,
    signal: AbortSignal,
  ): Promise<Pick<WeatherSnapshot, "temperatureC" | "windKph" | "condition">> {
    return this.forecastImpl(lat, lon, signal, this.forecastCallCount++);
  }
}

export const DEFAULT_FORECAST = { temperatureC: 20, windKph: 10, condition: "Clear sky" };

export function fixedGeo(name: string, country = "Testland"): GeoResult {
  return { name, country, latitude: 1, longitude: 2 };
}

// A promise plus its resolve/reject, pulled out for tests that need to
// control exactly when an in-flight call settles (the race-resolution and
// dedup tests both need this to force two lookups to overlap).
export function deferred<T>(): { promise: Promise<T>; resolve: (value: T) => void; reject: (err: unknown) => void } {
  let resolve!: (value: T) => void;
  let reject!: (err: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}
