# Learning: A Loading Screen in HTML and CSS

**Source:** "Build A Loading Screen" ([freeCodeCamp: How to build a delightful loading screen in 5 minutes](https://medium.freecodecamp.org/how-to-build-a-delightful-loading-screen-in-5-minutes-847991da509f)),
from the HTML and CSS section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial host was blocked from the build environment, so this follows the brief (a CSS-animated loading
screen that gives way to the page) and goes further: real progress, timing rules that stop flashing and
flickering, accessibility, reduced motion and an error/retry path.

> Written by the daily-project routine (Claude). Commits are authored as `Claude`, so they are separate
> from the repo owner's own work in the history.

Run: `npm run serve`, then open <http://localhost:8080/?latency=2500>. Other demos: `?latency=60` (too fast for
the overlay to appear), `?fail=content` or `?fail=hero` (error and retry).
Test: `npm test` (9 unit tests) and `NODE_PATH=$(npm root -g) npm run test:e2e` (5 tests in headless Chromium;
needs a global `playwright` install).

## Layout

- `src/loader.js`: no DOM access. `runLoader(tasks, options)` runs weighted tasks, reports progress and
  decides when the screen should show and hide. Time comes from an injected clock.
- `app.js`: the page wiring. Builds three demo tasks (fonts, JSON content, a decoded hero image), maps loader
  events onto the overlay's `data-state` (`idle → visible → leaving → done`, or `error`), and reveals the content.
- `style.css`: the spinner (a rotating ring plus three staggered dots), the progress bar, the fade-out,
  dark mode and `prefers-reduced-motion`.
- `index.html`: the overlay is a modal dialog with a `role="progressbar"` and an `aria-live` status line.
- `server.js`: static server on `node:http` (ES modules need an `http://` origin, not `file://`).
- `tests/loader.test.js`: timing and progress logic against a fake clock, so no real sleeps.
- `tests/page.test.js`: the real page in Chromium: slow load, fast load, failure and retry, reduced motion, no JS.

## What I learned

- **A loading screen can make a fast page feel slower.** If everything is ready in 80 ms, a spinner that
  flashes for one frame is worse than nothing. The overlay only appears once loading has run past
  `showDelayMs` (150 ms).
- **Once it's shown, keep it up for a minimum time.** Showing at 150 ms and hiding at 180 ms reads as a glitch.
  `remainingVisibleMs` keeps it up for at least `minVisibleMs` (600 ms) after it appears. Together the two
  rules give three outcomes: never shown, shown for at least the minimum, or shown until the work is done.
- **Weight your progress.** Counting tasks makes the bar jump 33% for a font and 33% for a large payload.
  Weighting by expected cost (1 / 3 / 2 here) makes the bar track real work. A failed task still counts as
  settled, so the bar never stalls below 100%.
- **`Promise.resolve().then(task.run)` turns a synchronous throw into a rejection**, so one bad task becomes a
  reported failure and doesn't crash the whole loader. A per-task timeout (`Promise.race`) does the same for a
  task that never settles.
- **Inject the clock.** With `now`/`setTimeout`/`clearTimeout` passed in, the unit tests step through time
  exactly (show at 150, hide at 750) in milliseconds of real time.
- **Animate `transform` and `opacity`, not `width`.** The bar uses `transform: scaleX(var(--progress))`, set as a
  CSS custom property from JS. The compositor can animate that without recalculating layout.
- **Fade out with `opacity` plus a delayed `visibility`.** `display: none` can't be transitioned. Transitioning
  `visibility` with a delay equal to the fade length keeps the overlay visible during the fade, then makes it
  invisible and lets clicks through.
- **Don't wait on `transitionend` alone.** It never fires when transitions are off (reduced motion) or the
  element is hidden, so `afterFade` also has a fallback timer. The reduced-motion browser test covers this.
- **Negative `animation-delay` staggers dots without a dead first beat.** Each dot starts partway through its
  cycle instead of waiting.
- **Respect `prefers-reduced-motion`.** The spinner stops and the fade becomes instant, but the status text and
  progress bar still say what's happening.
- **Accessibility needs more than a spinner.** The spinner is `aria-hidden`. Screen readers get an
  `aria-live="polite"` status line, a `progressbar` with `aria-valuenow`, and `aria-busy` on `<main>` until
  the content is ready. On error, focus moves to the retry button.
- **Plan for no JavaScript.** The page is hidden while loading only under an `html.js` class that an inline script
  sets before first paint. With JS off the overlay never appears and the content is readable.
- **Prefix checks on paths need a separator.** `resolved.startsWith(ROOT)` lets `/srv/app-evil` through for root
  `/srv/app`. The static server checks against `ROOT + path.sep`.

## Known limits

- The task weights are guesses. A real app could weight by `Content-Length`, or stream progress within a task
  using `ReadableStream`.
- Retry runs every task again, not only the ones that failed.
- The "fast load never shows the overlay" browser test depends on the machine finishing within 150 ms at
  `latency=0`. That's easy locally but could flake on a very slow CI runner.
