import { test, assert, assertEqual, type TestContext } from "./harness.js";
import { startFakeApi } from "./fakeApi.js";

function appUrl(base: string, params: Record<string, string>): string {
  const query = new URLSearchParams(params).toString();
  return `${base}/?${query}`;
}

async function statusState(ctx: TestContext): Promise<string | null> {
  return ctx.page.locator("#status").getAttribute("data-state");
}

test("search shows the result for a known city", async (ctx) => {
  const api = await startFakeApi();
  try {
    api.configure("paris", { latitude: 48.85, longitude: 2.35, country: "France", temperatureC: 21, windKph: 8 });
    await ctx.page.goto(appUrl(ctx.appBaseUrl, { geoBase: api.geoBase, forecastBase: api.forecastBase }));
    await ctx.page.fill("#city-input", "Paris");
    await ctx.page.click("button[type=submit]");
    await ctx.page.waitForSelector("#result h2");
    assertEqual(await ctx.page.textContent("#result h2"), "Paris, France");
    assertEqual(await ctx.page.textContent(".temperature"), "21°C");
  } finally {
    await api.close();
  }
});

test("toggling the unit re-renders the same result without a new fetch", async (ctx) => {
  const api = await startFakeApi();
  try {
    api.configure("berlin", { latitude: 52.5, longitude: 13.4, country: "Germany", temperatureC: 0 });
    await ctx.page.goto(appUrl(ctx.appBaseUrl, { geoBase: api.geoBase, forecastBase: api.forecastBase }));
    await ctx.page.fill("#city-input", "Berlin");
    await ctx.page.click("button[type=submit]");
    await ctx.page.waitForSelector("#result h2");

    await ctx.page.click("#unit-toggle");
    assertEqual(await ctx.page.textContent(".temperature"), "32°F");
    await ctx.page.click("#unit-toggle");
    assertEqual(await ctx.page.textContent(".temperature"), "0°C");

    assertEqual(api.callCount("berlin"), 1, "switching units must not re-fetch");
  } finally {
    await api.close();
  }
});

test("a city that fails twice then succeeds is retried, not shown as an error", async (ctx) => {
  const api = await startFakeApi();
  try {
    api.configure("flakyton", {
      latitude: 10,
      longitude: 10,
      country: "Testland",
      temperatureC: 15,
      failCount: 2,
      failStatus: 503,
    });
    await ctx.page.goto(appUrl(ctx.appBaseUrl, { geoBase: api.geoBase, forecastBase: api.forecastBase }));
    await ctx.page.fill("#city-input", "Flakyton");
    await ctx.page.click("button[type=submit]");
    await ctx.page.waitForSelector("#result h2", { timeout: 10000 });
    assertEqual(await ctx.page.textContent(".temperature"), "15°C");
    assertEqual(api.callCount("flakyton"), 3, "expected 2 failures + 1 success");
  } finally {
    await api.close();
  }
});

test("a nonexistent city is reported as not found, with no retry", async (ctx) => {
  const api = await startFakeApi();
  try {
    // Deliberately not configured -- the fake API's geocode falls through
    // to an empty `results` array, exactly like real Open-Meteo does for an
    // unknown place, which is what makes the app treat it as NotFoundError
    // rather than an HTTP failure.
    await ctx.page.goto(appUrl(ctx.appBaseUrl, { geoBase: api.geoBase, forecastBase: api.forecastBase }));
    await ctx.page.fill("#city-input", "Nowhereville");
    await ctx.page.click("button[type=submit]");
    await ctx.page.waitForFunction(() => document.querySelector("#status")?.getAttribute("data-state") === "error");
    const message = await ctx.page.textContent("#status");
    assert(message?.includes("Nowhereville"), `expected the not-found message to name the city, got: ${message}`);
    assertEqual(api.callCount("nowhereville"), 1, "a not-found result must not be retried");
  } finally {
    await api.close();
  }
});

test("a slow search superseded by a fast one never overwrites the fast result", async (ctx) => {
  const api = await startFakeApi();
  try {
    api.configure("paris", { latitude: 48.85, longitude: 2.35, country: "France", temperatureC: 21, delayMs: 1500 });
    api.configure("london", { latitude: 51.5, longitude: -0.12, country: "UK", temperatureC: 12, delayMs: 0 });
    await ctx.page.goto(appUrl(ctx.appBaseUrl, { geoBase: api.geoBase, forecastBase: api.forecastBase }));

    // Paris's response won't arrive for 1.5s; London's is immediate. Submit
    // Paris, then submit London well before Paris's response lands, and
    // confirm London's fast result sticks around instead of getting
    // clobbered when Paris's slow one finally arrives.
    await ctx.page.fill("#city-input", "Paris");
    await ctx.page.click("button[type=submit]");
    await ctx.page.fill("#city-input", "London");
    await ctx.page.click("button[type=submit]");

    await ctx.page.waitForSelector("#result h2");
    assertEqual(await ctx.page.textContent("#result h2"), "London, UK");

    await ctx.page.waitForTimeout(2000); // let Paris's delayed response land
    assertEqual(
      await ctx.page.textContent("#result h2"),
      "London, UK",
      "Paris's late response must not have overwritten London's",
    );
  } finally {
    await api.close();
  }
});

test("when a fetch fails after a successful one was cached, the stale result is shown with a badge", async (ctx) => {
  const api = await startFakeApi();
  try {
    api.configure("madrid", { latitude: 40.4, longitude: -3.7, country: "Spain", temperatureC: 25 });
    await ctx.page.goto(
      appUrl(ctx.appBaseUrl, { geoBase: api.geoBase, forecastBase: api.forecastBase, ttlMs: "300" }),
    );
    await ctx.page.fill("#city-input", "Madrid");
    await ctx.page.click("button[type=submit]");
    await ctx.page.waitForSelector("#result h2");
    assertEqual(await ctx.page.textContent(".temperature"), "25°C");

    // Now make every future request for Madrid fail, and wait past the
    // 300ms TTL so the next search actually re-fetches instead of serving
    // the still-fresh cache entry.
    api.configure("madrid", {
      latitude: 40.4,
      longitude: -3.7,
      country: "Spain",
      temperatureC: 25,
      failCount: 1000,
      failStatus: 503,
    });
    await ctx.page.waitForTimeout(350);
    await ctx.page.fill("#city-input", "Madrid");
    await ctx.page.click("button[type=submit]");

    await ctx.page.waitForFunction(() => document.querySelector("#status")?.getAttribute("data-state") === "stale", {
      timeout: 10000,
    });
    assertEqual(await ctx.page.textContent(".temperature"), "25°C", "the stale value should still be the last known reading");
    assertEqual(await ctx.page.getAttribute("#result", "data-stale"), "true");
  } finally {
    await api.close();
  }
});
