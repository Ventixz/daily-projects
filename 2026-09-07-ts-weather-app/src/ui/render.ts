import { formatTemperature } from "../core/units.js";
import type { TemperatureUnit } from "../core/units.js";
import type { WeatherResult } from "../types.js";

export interface Elements {
  form: HTMLFormElement;
  input: HTMLInputElement;
  unitToggle: HTMLButtonElement;
  status: HTMLElement;
  result: HTMLElement;
}

export function queryElements(root: ParentNode): Elements {
  const form = root.querySelector<HTMLFormElement>("#search-form");
  const input = root.querySelector<HTMLInputElement>("#city-input");
  const unitToggle = root.querySelector<HTMLButtonElement>("#unit-toggle");
  const status = root.querySelector<HTMLElement>("#status");
  const result = root.querySelector<HTMLElement>("#result");
  if (!form || !input || !unitToggle || !status || !result) {
    throw new Error("queryElements: a required DOM node is missing");
  }
  return { form, input, unitToggle, status, result };
}

export function renderLoading(els: Elements, query: string): void {
  els.status.textContent = `Searching for "${query}"…`;
  els.status.dataset.state = "loading";
}

export function renderError(els: Elements, message: string): void {
  els.status.textContent = message;
  els.status.dataset.state = "error";
  els.result.innerHTML = "";
  delete els.result.dataset.stale;
}

export function renderResult(els: Elements, weather: WeatherResult, unit: TemperatureUnit): void {
  els.status.textContent = weather.stale ? "Showing last known weather (offline)" : "";
  els.status.dataset.state = weather.stale ? "stale" : "ok";
  els.result.innerHTML = `
    <h2>${weather.city}, ${weather.country}</h2>
    <p class="temperature">${formatTemperature(weather.temperatureC, unit)}</p>
    <p class="condition">${weather.condition}</p>
    <p class="wind">Wind: ${Math.round(weather.windKph)} km/h</p>
  `;
  els.result.dataset.stale = String(weather.stale);
}

// Only the last call within any `delayMs` window actually invokes `fn`,
// exactly `delayMs` after that window's last call -- not on a fixed tick.
// The timer id is typed as `ReturnType<typeof setTimeout>` rather than
// `number` because this module is imported by both the browser app and the
// Node-based unit tests, and Node's setTimeout returns a Timeout object,
// not a number.
export function debounce<Args extends unknown[]>(
  fn: (...args: Args) => void,
  delayMs: number,
): (...args: Args) => void {
  let timer: ReturnType<typeof setTimeout> | undefined;
  return (...args: Args) => {
    if (timer !== undefined) clearTimeout(timer);
    timer = setTimeout(() => fn(...args), delayMs);
  };
}
