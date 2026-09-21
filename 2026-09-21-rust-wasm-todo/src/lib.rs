pub mod state;

// `dom` touches `web_sys`/`window()`, which only exists once this compiles to
// wasm32 and runs inside a browser. Gating it out of native builds is what lets
// `cargo test` exercise `state.rs` directly, no headless browser required.
#[cfg(target_arch = "wasm32")]
mod dom;
