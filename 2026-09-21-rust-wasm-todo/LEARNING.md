# Single Page Applications Using Rust (Rust → WebAssembly)

**Source:** ["Single Page Applications using Rust"](http://www.sheshbabu.com/posts/rust-wasm-yew-single-page-application/),
from the Rust section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial builds its SPA on Yew, a virtual-DOM framework modeled on
React/Elm. I built the same shape of thing — a client-routed, state-driven
page compiled from Rust to WebAssembly, no full page reloads — but skipped
Yew entirely and wired the DOM by hand with `wasm-bindgen`/`web-sys`. Yew
would have hidden the actual mechanism ("how does a `.wasm` blob end up
changing what's on screen") behind its own vdom diffing, which is exactly
the part worth seeing directly in a single-day project.

## What it is

A TodoMVC-style app, all client-side, with three files doing three
distinct jobs:

- `src/state.rs` — the entire domain: `Todo`, `AppState`, `Filter`, and
  every mutation (`add`/`toggle`/`remove`/`clear_completed`/`set_filter`).
  Zero `wasm-bindgen` or `web-sys` imports, so it's ordinary Rust that
  `cargo test` runs natively — no wasm32 target, no browser, no
  `wasm-bindgen-test`. It also owns `render(&self) -> String`, which
  builds the entire app's HTML from the current state as a plain string,
  and `serialize`/`deserialize`, a small hand-rolled (tab-delimited,
  backslash-escaped) text format for localStorage — deliberately not
  JSON, since the only consumer is this same Rust code and a real parser
  would be solving a problem this project doesn't have.
- `src/dom.rs` — the only module that calls `web_sys::window()`. Compiled
  only under `#[cfg(target_arch = "wasm32")]` (see `src/lib.rs`), so it
  physically cannot leak into the native test build. On startup it loads
  saved state from `localStorage`, reads the current `location.hash` for
  the active filter, does one `render`, and then wires exactly three
  event listeners for the rest of the page's life: a `click` handler and
  a `keydown` handler on `#app`, and a `hashchange` handler on `window`.
- `www/index.html` + `www/style.css` — a static shell with one
  `<div id="app">` and a `<script type="module">` that imports and calls
  the `wasm-bindgen`-generated `init()`. Everything inside `#app` is
  Rust's to own.

## Why it's worth building

- **Re-render by replacing `innerHTML`, not by diffing.** Every mutation
  calls `app.set_inner_html(&state.render())`. For a todo list that's a
  handful of `<li>`s, so throwing the whole subtree away and rebuilding
  it from a Rust `format!` string is cheaper than the bookkeeping a vdom
  needs to track *what* changed — and it makes the "framework" part of
  this SPA sit in about 130 lines instead of a dependency graph.
- **Rebuilding the DOM kills your event listeners, so delegate.** The
  first version of this attached a `keydown` listener directly to
  `#new-todo` — which works once, until the next re-render destroys that
  exact input element and creates a new one with no listener attached.
  The fix is standard event delegation: listen on `#app` (which is never
  replaced, only its children) and read `event.target()` inside the
  handler to figure out what was actually clicked or typed into. Every
  event handler in `dom.rs` follows this shape.
- **`set_inner_html` is an XSS foot-gun if you forget it.** A todo whose
  text is `<img src=x onerror=...>` gets parsed and executed by the
  browser unless the text is HTML-escaped before it goes into the
  string. `render()` escapes every todo's text through `escape_html`;
  the e2e suite (below) adds exactly that todo and asserts the injected
  `onerror` never ran, so this isn't just a comment — it's enforced.
- **`location.hash` and the `hashchange` *event* update at different
  times, and that gap is a real bug source.** Clicking `<a href="#/active">`
  updates `location.hash` synchronously as part of the browser's default
  navigation, but `hashchange` fires on a later task. The first version
  of the Playwright e2e test waited on `location.hash` alone and read
  the DOM immediately after — and flaked, catching the *previous*
  filter's render because `dom.rs`'s `hashchange` listener (the thing
  that actually calls `render()`) hadn't run yet. The fix was waiting on
  the rendered output itself (the `.filters a.selected` link) rather
  than the raw hash — a reminder that "the URL changed" and "the app
  reacted to the URL changing" are two different, non-simultaneous facts.

## Setup (Windows 11 / PowerShell)

```powershell
winget install --id Rustlang.Rustup -e
# reopen PowerShell so cargo/rustup are on PATH, then:
rustup target add wasm32-unknown-unknown
cargo install wasm-bindgen-cli --version 0.2.128 --locked
winget install --id OpenJS.NodeJS.LTS -e
# reopen PowerShell again, then, inside the project folder:
npm install
npx playwright install chromium
cargo --version; wasm-bindgen --version
```

`wasm-bindgen-cli`'s version must match the `wasm-bindgen` crate version
`Cargo.lock` resolves (0.2.128 here) — a mismatch fails at build time
with a clear "schema version mismatch" error, not a silent bug.

## Build it

```
make build
```

Compiles `src/` to `target/wasm32-unknown-unknown/release/rust_wasm_todo.wasm`,
then runs `wasm-bindgen --target web` to emit the JS glue module and the
final `.wasm` into `www/pkg/` (gitignored — a build output, not source).

## Run it

```
make run
```

Serves `www/` on `http://localhost:8080` — open it, add some todos, filter
by Active/Completed, reload the page and confirm they're still there.

## Test it

```
make test   # 12 cargo tests over state.rs, native target, no browser
make e2e    # builds, serves www/, drives the real page with Playwright
```

`make test` covers every state transition and both serialization
directions directly. `make e2e` is the only check that `dom.rs`'s actual
wiring — event delegation, hash routing, localStorage round-tripping
through a page reload, and the HTML-escaping — works together in a real
browser, which `cargo test` structurally can't see since `dom.rs` isn't
even compiled into that build.

## Stretch goals (not implemented)

- Editing a todo's text in place (double-click to edit, like the real
  TodoMVC spec) — `state.rs` would need an `edit(id, new_text)` mutation
  and `dom.rs` a `dblclick` handler plus a way to distinguish "editing"
  from "display" per row in `render()`.
- A `console_error_panic_hook` so a Rust panic inside `dom.rs` shows a
  real stack trace in the browser console instead of an opaque
  `unreachable` trap.

## Hints

- `web-sys` gates every type behind a Cargo feature — forgetting
  `"DomTokenList"` (needed for `Element::class_list().contains(...)`) or
  `"Location"` (for `window.location().hash()`) fails at compile time
  with "no method named `class_list` found", which reads like a typo but
  is actually a missing feature flag in `Cargo.toml`.
- `Closure::forget()` intentionally leaks the closure so the JS side can
  keep calling it after the Rust function that created it returns — the
  alternative (dropping it) would deallocate it while `#app` still holds
  a live reference, and the next click would call into freed memory.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Tutorial: ["Single Page Applications using Rust"](http://www.sheshbabu.com/posts/rust-wasm-yew-single-page-application/) by Sheshbabu Chinnakonda.
