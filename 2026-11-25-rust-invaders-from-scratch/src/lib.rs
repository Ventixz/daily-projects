//! Starfall: an invaders-style arcade shooter built from scratch.
//!
//! The pieces, bottom-up:
//! - [`sprite`]: 1-bit bitmaps and pixel-exact overlap tests.
//! - [`art`]: the hand-drawn sprites, written as ASCII art.
//! - [`buffer`]: a CPU framebuffer of `0x00RRGGBB` pixels.
//! - [`font`]: a 5x7 bitmap font for the score and messages.
//! - [`game`]: the whole game as a deterministic state machine,
//!   advanced one fixed 1/60 s tick at a time by [`game::Game::update`].
//! - [`render`]: draws a [`game::Game`] into a [`buffer::Buffer`].
//! - [`autopilot`]: a simple bot, used for the headless demo and tests.
//! - [`png`]: a small PNG encoder (fixed-Huffman deflate) for screenshots.
//!
//! Nothing here depends on a window. The `window` feature adds a
//! minifb frontend in `main.rs` that copies the buffer to the screen.

pub mod art;
pub mod autopilot;
pub mod buffer;
pub mod font;
pub mod game;
pub mod png;
pub mod render;
pub mod rng;
pub mod sprite;
