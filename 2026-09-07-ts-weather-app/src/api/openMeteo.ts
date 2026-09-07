import { HttpError, NotFoundError } from "../types.js";
import type { WeatherApi, GeoResult, WeatherSnapshot } from "../types.js";

// Open-Meteo (open-meteo.com) needs no API key -- unlike the tutorial this
// project started from (which uses OpenWeatherMap and a per-developer key),
// so the app runs end to end for anyone who clones it with nothing to sign
// up for or keep secret.

// WMO weather interpretation codes (the numeric `weathercode` field the
// forecast endpoint returns) collapsed to the ranges Open-Meteo's own docs
// group them into, mapped to a short human label.
const WMO_CONDITIONS: Array<[max: number, label: string]> = [
  [0, "Clear sky"],
  [1, "Mainly clear"],
  [2, "Partly cloudy"],
  [3, "Overcast"],
  [48, "Fog"],
  [55, "Drizzle"],
  [65, "Rain"],
  [77, "Snow"],
  [82, "Rain showers"],
  [86, "Snow showers"],
  [99, "Thunderstorm"],
];

export function describeWeatherCode(code: number): string {
  for (const [max, label] of WMO_CONDITIONS) {
    if (code <= max) return label;
  }
  return "Unknown";
}

async function parseJsonOrThrow(response: Response): Promise<unknown> {
  if (!response.ok) {
    throw new HttpError(`${response.status} ${response.statusText}`, response.status);
  }
  return response.json();
}

export class OpenMeteoApi implements WeatherApi {
  constructor(
    private geocodeBase = "https://geocoding-api.open-meteo.com/v1/search",
    private forecastBase = "https://api.open-meteo.com/v1/forecast",
  ) {}

  async geocode(query: string, signal: AbortSignal): Promise<GeoResult> {
    const url = `${this.geocodeBase}?name=${encodeURIComponent(query)}&count=1`;
    const response = await fetch(url, { signal });
    const body = (await parseJsonOrThrow(response)) as { results?: GeoResult[] };
    const first = body.results?.[0];
    if (!first) throw new NotFoundError(query);
    return first;
  }

  async forecast(
    lat: number,
    lon: number,
    signal: AbortSignal,
  ): Promise<Pick<WeatherSnapshot, "temperatureC" | "windKph" | "condition">> {
    const url = `${this.forecastBase}?latitude=${lat}&longitude=${lon}&current_weather=true`;
    const response = await fetch(url, { signal });
    const body = (await parseJsonOrThrow(response)) as {
      current_weather?: { temperature: number; windspeed: number; weathercode: number };
    };
    const current = body.current_weather;
    if (!current) throw new HttpError("forecast response missing current_weather", response.status);
    return {
      temperatureC: current.temperature,
      windKph: current.windspeed,
      condition: describeWeatherCode(current.weathercode),
    };
  }
}
