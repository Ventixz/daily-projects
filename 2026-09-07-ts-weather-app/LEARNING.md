# Build a Simple Weather App (TypeScript)

**Source:** ["Build a Simple Weather App With Vanilla JavaScript"](https://www.freecodecamp.org/news/build-a-weather-app-with-vanilla-javascript/),
from the JavaScript section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial's brief is a text box, a `fetch` to a weather API, and writing
the JSON straight into the DOM. I kept that shape -- one input, one result
panel, no framework -- but ported it to TypeScript and swapped in the parts
that make "search a city, show its weather" actually interesting to build
properly: a real API that needs no signup key (Open-Meteo, not
OpenWeatherMap), exponential backoff with full jitter instead of "fetch
once and give up", request de-duplication and in-flight cancellation
instead of a fresh unguarded `fetch` per keystroke, and a stale-cache
fallback instead of a blank screen the moment the network hiccups.

## What it is

- `src/core/units.ts` -- pure Celsius/Fahrenheit conversion and display
  formatting. Weather is stored as Celsius everywhere and converted only at
  render time, so toggling the unit is a pure re-render with no re-fetch.
- `src/core/cache.ts` -- a TTL cache with two read methods on purpose:
  `get` (undefined once expired) for "can I skip the network", and
  `getStale` (returns the value even past expiry) for "the network just
  failed -- what's the best answer I still have."
- `src/core/backoff.ts` -- `computeDelayMs` (exponential backoff with full
  jitter: `random(0, min(maxMs, base*2^attempt))`, not `random` defaulting
  to `Math.random` so a test can pin it and assert exact delays) and
  `isRetryableStatus` (5xx/429/no-response are transient; any other 4xx and
  the not-found case aren't).
- `src/core/weatherClient.ts` -- the orchestration layer: retries with
  backoff, de-dupes identical concurrent queries onto one network round
  trip, cancels a superseded request, serves a fresh cache hit with zero
  network calls, and falls back to a stale cache entry rather than an
  error when a live fetch is exhausted. This is where both real bugs in
  this project (below) were caught, both by tests, both before the e2e
  suite ever touched a browser.
- `src/api/openMeteo.ts` -- the actual transport: `WeatherApi` implemented
  against Open-Meteo's geocoding + forecast endpoints (no API key, unlike
  the tutorial's OpenWeatherMap), including the WMO weather-code table.
- `src/ui/render.ts` -- DOM rendering (loading/error/result/stale states)
  and a generic `debounce`.
- `app.ts` -- wires it together, reading `geoBase`/`forecastBase`/`ttlMs`
  from the query string so the e2e suite can point the app at a local fake
  API and a short TTL instead of the real network.
- `server.js` -- a static file server on `node:http`/`node:fs` only, used
  for `make serve` and as the origin the e2e tests point a real browser at.
- `tests/unit/` -- 26 hand-rolled assertions against the pure core,
  including 10 against `WeatherClient` alone (dedup, race resolution,
  retry, non-retryable short-circuit, `NotFoundError` handling, fresh-cache
  hit, stale fallback, exhaustion).
- `tests/e2e/` -- 6 Playwright-driven browser tests against a fake weather
  API (`tests/e2e/fakeApi.ts`, a controllable stand-in for Open-Meteo with
  scriptable failures/delays and a `/__stats` endpoint) run in real
  Chromium.

## Run it

```bash
cd 2026-09-07-ts-weather-app
make unit    # 26 assertions
make e2e     # 6 browser tests via the globally-installed playwright
make test    # both
make serve   # http://localhost:8080
```

`make e2e` needs a `playwright` install reachable from `NODE_PATH` (this
project's own dependencies are just `typescript` and `@types/node` -- see
`tests/e2e/harness.ts` for why `require`, not `import`, is what makes
borrowing the global copy possible).

## What it actually teaches

- **A monotonic "latest request wins" counter is the wrong tool for race
  resolution when two calls can be for the *same* thing.** My first cut of
  `WeatherClient.lookup` assigned each call a `++requestId` and discarded
  any result whose id wasn't still the newest by the time it resolved --
  textbook "ignore stale responses." It broke `tests/unit/weatherClient.test.ts`'s
  dedup test immediately: two concurrent `lookup("Paris")` calls share one
  network round trip, but the *second* call still bumps the counter past
  the *first* call's id, so the first call's await sees `requestId !==
  latestRequestId` and returns `null` -- a query that was never actually
  superseded gets thrown away anyway, because "another lookup happened"
  and "another lookup for something *else* happened" aren't the same
  condition. The fix was to key supersession on the normalized query
  string instead of a counter (`this.latestKey`, compared at resolution
  time): two calls for the same city both set it to the same value and
  both survive; a call for a different city moves it and only that
  difference discards a result. `weatherClient.test.ts`'s dedup and race
  tests are the two halves of the same distinction -- one asserts sharing,
  the other asserts discarding -- and neither passes under the counter
  version.
- **Deferred promises can force a genuine race in a unit test, without
  fake timers or a real network.** `tests/unit/fakeWeatherApi.ts`'s
  `deferred()` hands a test manual control over exactly when an in-flight
  "fetch" resolves. The race test starts `lookup("Paris")` against a
  geocode call that blocks on a deferred promise, then `await`s
  `lookup("London")` against one that resolves immediately, *then* releases
  Paris -- reproducing "type a slow city, then a fast one, and the slow
  one's answer arrives last" deterministically, in milliseconds, with no
  `setTimeout` anywhere in the test itself.
- **A cache class with only one read method can't do stale-while-revalidate.**
  `TtlCache` deliberately has both `get` (undefined past expiry) and
  `getStale` (returns the value regardless of expiry). Building the offline
  fallback made it obvious why: `WeatherClient.lookup`'s success path calls
  `get` (skip the network if still fresh), but its failure path calls
  `getStale` on the exact same key (an old answer beats no answer). One
  cache, two questions -- collapsing them into one `get` would have forced
  a choice between "correct freshness" and "correct fallback," instead of
  getting both.
- **The offline-fallback test only exercises the fallback if the fetch
  actually happens** -- which means it has to defeat the fresh-cache
  short-circuit it just described above. `tests/e2e/app.spec.ts`'s stale
  test searches Madrid successfully, reconfigures the fake API to fail
  every request for Madrid from then on, and then has to wait past the
  app's `ttlMs` (passed in via the query string, shrunk to 300ms for the
  test) before searching Madrid again -- searching immediately would just
  hit the fresh cache and never touch the network at all, proving nothing
  about the fallback path. This is the same "reload has to know what it's
  actually waiting for" lesson `2026-09-06-js-todo-list` hit with its
  IndexedDB write -- here it's a TTL instead of an async write, but the
  fix is the same shape: make the timing the test depends on an explicit,
  controllable parameter instead of a hope.
- **A cross-origin fake server needs the same CORS header the real API
  sends, or the browser blocks it before your code ever sees the
  response -- and the failure looks exactly like a network outage.**
  The first real run of the e2e suite hung on every test: `page.fill`
  timing out looking for `#city-input` (a stale-server-root bug, fixed
  separately -- see `server.js`'s `findProjectRoot`), and then, once that
  was fixed, `page.waitForSelector('#result h2')` timing out with the
  not-found test showing 4 requests where 1 was expected. Chromium's
  console showed the real cause: `tests/e2e/fakeApi.ts` runs on a
  different port than the app (deliberately, to mirror hitting a real
  external API), and without `Access-Control-Allow-Origin`, `fetch()`
  rejects the response with a generic network-style error *before*
  `openMeteo.ts` ever gets a status code to inspect. `isRetryableStatus`
  correctly treats "no status" as transient (a real DNS failure should be
  retried) -- so a missing CORS header on the *test's own fake server*
  silently turned a should-fail-once test into a retry-four-times-then-fail
  test, exercising the wrong code path for the wrong reason. One
  `res.setHeader("Access-Control-Allow-Origin", "*")` line fixed all four
  then-failing tests at once, which is itself the tell that they were
  failing on the same root cause rather than four separate ones.

## Deliberate scope cuts

- **No geolocation ("weather near me").** The tutorial doesn't have it
  either; adding it would mean mocking `navigator.geolocation` in the e2e
  suite for no gain over what the city-search path already proves about
  the client's retry/cache/race logic.
- **No request timeout independent of retry.** A single attempt that hangs
  forever (rather than failing fast) is currently only bounded by whatever
  the browser's own `fetch` does. `WeatherApi` methods take an
  `AbortSignal`, so wiring a per-attempt timeout through it is additive,
  not a redesign.
- **Backoff is per-`WeatherClient` instance, not global.** Two independent
  `WeatherClient`s hammering the same real API wouldn't coordinate; this
  app only ever constructs one, so it wasn't worth the shared-state
  complexity to solve a problem the app doesn't have.

## What I'd add next

- **A visible retry indicator.** Right now a flaky fetch just shows the
  loading state for longer -- `tests/e2e/app.spec.ts`'s flaky-city test
  passes without the UI ever revealing that a retry happened. Surfacing
  attempt count during the wait would make the backoff visible, not just
  correct.
- **Per-attempt timeout**, per the scope cut above -- worth doing before
  this ever talks to a real flaky network instead of a fake one.
- **A `country`+`name` disambiguator** for cities that collide (there's
  more than one Springfield); Open-Meteo's geocoding endpoint returns up to
  `count` matches and this client always takes the first.
