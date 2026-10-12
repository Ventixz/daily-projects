# Learning: A 6502 CPU Core in Rust (NES Emulator, CPU chapter)

**Source:** ["Writing NES Emulator in Rust"](https://bugzmanov.github.io/nes_ebook/) from the Rust
section of [practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
I built the CPU part only (the book's standalone first deliverable) using just the Rust standard library.

Run the demo with `cargo run` and the tests with `cargo test`.

## Layout

- `src/lib.rs` — the `Cpu` (registers, flags, 64 KiB memory), a table of all 151 official opcodes
  as `(byte, Op, Mode)`, and `step` / `run` / `load_and_run`.
- `src/main.rs` — a hand-assembled program that computes 6 × 7 with a loop.
- `tests/cpu.rs` — 21 tests: flags, every addressing-mode family, branches, stack, JSR/RTS, BRK/RTI,
  and an integration program.

## What I learned

- **Table-driven decoding beats a 151-arm match.** Pairing an operation with an addressing mode
  separates "where is the operand" from "what to do with it", so `LDA` has one implementation, not eight.
- **Subtraction is addition of the complement.** `SBC m` is `ADC !m`; carry means "no borrow".
- **Overflow is not carry.** V is set when both inputs share a sign and the result's differs:
  `(a ^ result) & (m ^ result) & 0x80`.
- **The 6502 has quirks that tests must pin down.** Zero-page indexing wraps inside page zero;
  `JMP ($xxFF)` reads its high byte from `$xx00`; `JSR` pushes the address of its *last* byte, so `RTS` adds 1;
  `BRK` skips a padding byte; `PHP` sets the B flag in the pushed copy only.
- **A hand-assembled test program is easy to get wrong.** My first integration test failed because I
  miscounted a branch offset, not because of the CPU. Computing offsets from "address after the operand" matters.

## Known limits

- No cycle counting or page-crossing penalties, so it can't drive PPU/APU timing.
- No illegal/undocumented opcodes, no decimal mode (correct for the NES's 2A03), no interrupts other than BRK.
- Not validated against the `nestest` ROM; correctness rests on my own tests.
- No bus/cartridge/PPU yet — memory is a flat array.
