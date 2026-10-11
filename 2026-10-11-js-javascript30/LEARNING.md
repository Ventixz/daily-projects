# Learning: JavaScript30 Trio (Drum Kit, Clock, Type Ahead)

**Source:** ["Build 30 things in 30 days with 30 tutorials"](https://javascript30.com) from the
JavaScript section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
I built three of the 30 exercises (Drum Kit, JS + CSS Clock, Type Ahead) as one dependency-free page.

Run the tests with `node --test` and open `index.html` in a browser.

## Layout

- `lib.js` — pure logic: `padForKey`, `clockAngles`, `filterItems`, `highlight`, `withCommas`.
  Works under Node (`module.exports`) and in the browser (`window.Lib`).
- `test.js` — 8 `node:test` tests.
- `index.html` — DOM wiring only; drum sounds are synthesized with WebAudio, so no sample files.

## What I learned

- **Separate logic from the DOM.** Keeping the math and filtering in `lib.js` made it testable
  in Node without a browser; the page is just glue.
- **Clock hands need a +90° offset** because a CSS-rotated element starts at 3 o'clock.
  Feeding minutes into the hour hand (and seconds into the minute hand) makes motion continuous.
- **User input must be escaped before becoming a RegExp.** Typing `(` or `.` in the type-ahead
  box otherwise throws or matches the wrong thing; tests cover both.
- **Object lookups need `hasOwnProperty`.** `PADS['toString']` is truthy via the prototype.
- **A smoke test caught a real bug the unit tests could not:** a top-level `function padForKey`
  in `lib.js` clashed with `const { padForKey }` in the page script (global redeclaration).
  Wrapping the page script in an IIFE fixed it.
- **Browsers block audio until a user gesture**, so the `AudioContext` is created lazily on
  the first keypress.

## Known limits

- Uses the deprecated `keyCode`; `event.code` would be more robust across layouts.
- Only 3 of the 30 exercises; the type-ahead list is a small hard-coded array, not a fetch.
- No automated browser tests; the page was only smoke-tested once with Playwright.
