// A controllable stand-in for Open-Meteo's geocoding + forecast endpoints,
// speaking the same request/response shape as src/api/openMeteo.ts expects.
// Per-city behavior is configured with `configure()` before a test starts,
// and `/__stats` exposes call counts so a test can assert *how many* times
// the app actually hit the network (e.g. "retried exactly twice", "did not
// re-fetch a cached city") without instrumenting the app itself.
import http from "node:http";

interface CityConfig {
  latitude: number;
  longitude: number;
  country: string;
  temperatureC: number;
  windKph: number;
  weathercode: number;
  // Number of leading requests to fail with this status before succeeding.
  // 0 means never fail.
  failCount: number;
  failStatus: number;
  delayMs: number;
}

const DEFAULT_CITY: Omit<CityConfig, "latitude" | "longitude" | "country"> = {
  temperatureC: 18,
  windKph: 12,
  weathercode: 1,
  failCount: 0,
  failStatus: 503,
  delayMs: 0,
};

export interface FakeApiHandle {
  baseUrl: string;
  geoBase: string;
  forecastBase: string;
  configure(city: string, config: Partial<CityConfig> & { latitude: number; longitude: number; country: string }): void;
  callCount(city: string): number;
  close(): Promise<void>;
}

export function startFakeApi(): Promise<FakeApiHandle> {
  const cities = new Map<string, CityConfig>();
  const calls = new Map<string, number>();

  function sleep(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  return new Promise((resolve) => {
    const server = http.createServer(async (req, res) => {
      // The app page and this fake API are deliberately on different ports
      // (different origins), same as the real app talking to
      // api.open-meteo.com -- so this needs the same permissive CORS header
      // Open-Meteo itself sends, or the browser's fetch() rejects every
      // response before app code ever sees it.
      res.setHeader("Access-Control-Allow-Origin", "*");
      const url = new URL(req.url ?? "/", "http://localhost");

      if (url.pathname === "/__stats") {
        const city = (url.searchParams.get("city") ?? "").toLowerCase();
        res.writeHead(200, { "Content-Type": "application/json" }).end(JSON.stringify({ calls: calls.get(city) ?? 0 }));
        return;
      }

      if (url.pathname === "/v1/search") {
        const name = url.searchParams.get("name") ?? "";
        const key = name.toLowerCase();
        const config = cities.get(key);
        const n = (calls.get(key) ?? 0) + 1;
        calls.set(key, n);

        if (!config) {
          res.writeHead(200, { "Content-Type": "application/json" }).end(JSON.stringify({ results: [] }));
          return;
        }
        if (config.delayMs) await sleep(config.delayMs);
        if (n <= config.failCount) {
          res.writeHead(config.failStatus).end("injected failure");
          return;
        }
        res.writeHead(200, { "Content-Type": "application/json" }).end(
          JSON.stringify({
            results: [{ name, country: config.country, latitude: config.latitude, longitude: config.longitude }],
          }),
        );
        return;
      }

      if (url.pathname === "/v1/forecast") {
        // The app requests forecast by lat/lon; find the matching city by
        // coordinates rather than tracking a second identity.
        const lat = Number(url.searchParams.get("latitude"));
        const lon = Number(url.searchParams.get("longitude"));
        const found = [...cities.values()].find((c) => c.latitude === lat && c.longitude === lon);
        if (!found) {
          res.writeHead(404).end("unknown coordinates");
          return;
        }
        res.writeHead(200, { "Content-Type": "application/json" }).end(
          JSON.stringify({
            current_weather: {
              temperature: found.temperatureC,
              windspeed: found.windKph,
              weathercode: found.weathercode,
            },
          }),
        );
        return;
      }

      res.writeHead(404).end("not found");
    });

    server.listen(0, "127.0.0.1", () => {
      const address = server.address();
      const port = typeof address === "object" && address ? address.port : 0;
      const baseUrl = `http://127.0.0.1:${port}`;

      resolve({
        baseUrl,
        geoBase: `${baseUrl}/v1/search`,
        forecastBase: `${baseUrl}/v1/forecast`,
        configure(city, config) {
          cities.set(city.toLowerCase(), { ...DEFAULT_CITY, ...config });
        },
        callCount(city) {
          return calls.get(city.toLowerCase()) ?? 0;
        },
        close() {
          return new Promise((res) => server.close(() => res()));
        },
      });
    });
  });
}
