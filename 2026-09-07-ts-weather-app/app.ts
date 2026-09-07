import { OpenMeteoApi } from "./src/api/openMeteo.js";
import { WeatherClient } from "./src/core/weatherClient.js";
import { queryElements, renderLoading, renderError, renderResult, debounce } from "./src/ui/render.js";
import { NotFoundError, LookupFailedError } from "./src/types.js";
import type { TemperatureUnit } from "./src/core/units.js";
import type { WeatherResult } from "./src/types.js";

const DEBOUNCE_MS = 300;

// Reads geoBase/forecastBase/ttlMs from the query string so e2e tests can
// point the app at a local fake API and shrink the cache TTL, instead of
// mocking `fetch` globally -- see tests/e2e/fakeApi.ts and its use of these
// same param names.
function boot(): void {
  const params = new URLSearchParams(location.search);
  const api = new OpenMeteoApi(params.get("geoBase") ?? undefined, params.get("forecastBase") ?? undefined);
  const ttlParam = params.get("ttlMs");
  const client = new WeatherClient(api, ttlParam ? { ttlMs: Number(ttlParam) } : {});
  const els = queryElements(document);

  let unit: TemperatureUnit = "celsius";
  let lastResult: WeatherResult | null = null;

  async function search(query: string): Promise<void> {
    const trimmed = query.trim();
    if (!trimmed) return;
    renderLoading(els, trimmed);
    try {
      const result = await client.lookup(trimmed);
      if (result === null) return; // superseded by a newer search; leave the newer render alone
      lastResult = result;
      renderResult(els, result, unit);
    } catch (err) {
      if (err instanceof NotFoundError) {
        renderError(els, err.message);
      } else if (err instanceof LookupFailedError) {
        renderError(els, `Couldn't reach the weather service for "${err.query}". Try again in a moment.`);
      } else {
        renderError(els, "Something went wrong.");
      }
    }
  }

  const debouncedSearch = debounce(search, DEBOUNCE_MS);

  els.input.addEventListener("input", () => debouncedSearch(els.input.value));
  els.form.addEventListener("submit", (event) => {
    event.preventDefault();
    void search(els.input.value);
  });

  els.unitToggle.addEventListener("click", () => {
    unit = unit === "celsius" ? "fahrenheit" : "celsius";
    els.unitToggle.textContent = unit === "celsius" ? "Show °F" : "Show °C";
    if (lastResult) renderResult(els, lastResult, unit);
  });
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", boot);
} else {
  boot();
}
