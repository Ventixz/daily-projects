import { test, assert, assertEqual, assertRejects } from "./harness.js";
import { WeatherClient } from "../../src/core/weatherClient.js";
import { HttpError, NotFoundError, LookupFailedError } from "../../src/types.js";
import { ScriptedWeatherApi, DEFAULT_FORECAST, fixedGeo, deferred } from "./fakeWeatherApi.js";

// Every test injects sleep/random so retries never actually wait in real
// time and their delays are deterministic -- see backoff.test.ts for the
// pure delay math itself; these tests only care that the client calls it
// the right number of times.
const instantSleep = async () => {};
const noJitter = () => 0;

test("lookup: a successful fetch returns a non-stale result", async () => {
  const api = new ScriptedWeatherApi(async () => fixedGeo("Paris"));
  const client = new WeatherClient(api, { sleep: instantSleep, random: noJitter });
  const result = await client.lookup("paris");
  assert(result !== null);
  assertEqual(result.city, "Paris");
  assertEqual(result.stale, false);
});

test("lookup: a fresh cache hit does not call the API again", async () => {
  const api = new ScriptedWeatherApi(async () => fixedGeo("Paris"));
  const client = new WeatherClient(api, { sleep: instantSleep, random: noJitter });
  await client.lookup("Paris");
  await client.lookup("  paris  "); // different casing/whitespace, same normalized key
  assertEqual(api.geocodeCallCount, 1);
});

test("lookup: two concurrent calls for the same query share one API call", async () => {
  const gate = deferred<void>();
  const api = new ScriptedWeatherApi(async () => {
    await gate.promise;
    return fixedGeo("Paris");
  });
  const client = new WeatherClient(api, { sleep: instantSleep, random: noJitter });

  const first = client.lookup("Paris");
  const second = client.lookup("Paris");
  gate.resolve();
  const [a, b] = await Promise.all([first, second]);

  assertEqual(api.geocodeCallCount, 1);
  assert(a !== null && b !== null);
  assertEqual(a.city, "Paris");
  assertEqual(b.city, "Paris");
});

test("lookup: a superseded call resolves to null instead of the stale query's result", async () => {
  const parisGate = deferred<void>();
  const api = new ScriptedWeatherApi(async (query) => {
    if (query === "Paris") {
      await parisGate.promise;
      return fixedGeo("Paris");
    }
    return fixedGeo("London");
  });
  const client = new WeatherClient(api, { sleep: instantSleep, random: noJitter });

  const parisResult = client.lookup("Paris"); // starts, then blocks on parisGate
  const londonResult = await client.lookup("London"); // resolves first, becomes "the" latest search

  assert(londonResult !== null);
  assertEqual(londonResult.city, "London");

  parisGate.resolve(); // Paris's response finally arrives, after London already rendered
  assertEqual(await parisResult, null, "a result for a search the user has since replaced must not win the race");
});

test("lookup: retries a transient failure and succeeds before exhausting attempts", async () => {
  const api = new ScriptedWeatherApi(async (_query, _signal, call) => {
    if (call < 2) throw new HttpError("service unavailable", 503);
    return fixedGeo("Paris");
  });
  const client = new WeatherClient(api, { sleep: instantSleep, random: noJitter });
  const result = await client.lookup("Paris");
  assert(result !== null);
  assertEqual(result.city, "Paris");
  assertEqual(api.geocodeCallCount, 3);
});

test("lookup: a non-retryable status fails on the first attempt", async () => {
  const api = new ScriptedWeatherApi(async () => {
    throw new HttpError("bad request", 400);
  });
  const client = new WeatherClient(api, { sleep: instantSleep, random: noJitter });
  await assertRejects(client.lookup("Paris"), LookupFailedError);
  assertEqual(api.geocodeCallCount, 1);
});

test("lookup: NotFoundError is not retried and is not masked by a stale cache entry", async () => {
  let call = 0;
  const api = new ScriptedWeatherApi(async (query) => {
    call++;
    if (query === "Paris") return fixedGeo("Paris");
    throw new NotFoundError(query);
  });
  const client = new WeatherClient(api, { sleep: instantSleep, random: noJitter });

  await client.lookup("Paris"); // seed the cache with an unrelated entry
  await assertRejects(client.lookup("Nowhereville"), NotFoundError);
  assertEqual(call, 2, "NotFoundError must not trigger a retry");
});

test("lookup: falls back to a stale cache entry when a later fetch exhausts its retries", async () => {
  let now = 0;
  const api = new ScriptedWeatherApi(async (_query, _signal, call) => {
    if (call === 0) return fixedGeo("Paris"); // first lookup: succeeds, gets cached
    throw new HttpError("service unavailable", 503); // every lookup after that: fails
  });
  const client = new WeatherClient(api, { sleep: instantSleep, random: noJitter, ttlMs: 100, now: () => now });

  const first = await client.lookup("Paris");
  assert(first !== null && first.stale === false);

  now = 1000; // past the TTL, so the second lookup actually re-fetches
  const second = await client.lookup("Paris");
  assert(second !== null);
  assertEqual(second.stale, true);
  assertEqual(second.city, "Paris");
});

test("lookup: throws LookupFailedError when every attempt fails and nothing is cached", async () => {
  const api = new ScriptedWeatherApi(async () => {
    throw new HttpError("service unavailable", 503);
  });
  const client = new WeatherClient(api, {
    sleep: instantSleep,
    random: noJitter,
    backoff: { baseMs: 1, maxMs: 1, maxAttempts: 3 },
  });
  await assertRejects(client.lookup("Nowhere"), LookupFailedError);
  assertEqual(api.geocodeCallCount, 3);
});

test("lookup: forecast failures are retried the same way as geocode failures", async () => {
  let forecastCalls = 0;
  const api = new ScriptedWeatherApi(
    async () => fixedGeo("Paris"),
    async () => {
      forecastCalls++;
      if (forecastCalls < 2) throw new HttpError("service unavailable", 503);
      return DEFAULT_FORECAST;
    },
  );
  const client = new WeatherClient(api, { sleep: instantSleep, random: noJitter });
  const result = await client.lookup("Paris");
  assert(result !== null);
  assertEqual(forecastCalls, 2);
  // A retried attempt redoes geocode too, not just forecast -- the pair is
  // one unit of work per attempt, since the client has no way to cache a
  // successful geocode independent of the forecast that followed it.
  assertEqual(api.geocodeCallCount, 2);
});
