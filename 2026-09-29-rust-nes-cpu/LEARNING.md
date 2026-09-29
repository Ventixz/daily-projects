# Writing NES Emulator in Rust (Rust)

**Source:** ["Writing NES Emulator in Rust"](https://bugzmanov.github.io/nes_ebook/)
by bugzmanov, from the Rust section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
(The book builds a full NES emulator -- CPU, PPU, APU, joypads. This project
covers its first stage, the MOS 6502 CPU core, which is where most of the
subtle bugs live, and runs it against a flat 64 KiB memory.)

No dependencies beyond the standard library.

## What it is

- `src/lib.rs` -- a `Cpu` with registers, status flags, stack, ~150 official
  opcodes across all addressing modes, plus 14 unit tests.
- `src/main.rs` -- a demo 6502 program that sums 1..=10 with a loop and
  stores the result at `$0200`.

## Run it

```bash
cd 2026-09-29-rust-nes-cpu
cargo test     # 14 tests
cargo run      # A = 55, mem[$0200] = 55
```

## What it actually teaches

- **SBC is just ADC of the complement.** `v` becomes `!v` and the carry flag
  acts as an inverted borrow. One `adc` function serves both, and the
  `sbc_borrow` test pins down that `3 - 5` clears carry.
- **Overflow is not carry.** Carry means unsigned wraparound; V means the
  signed result is wrong: `(a ^ r) & (v ^ r) & 0x80`. `0x50 + 0x50` sets V but
  not C, and `0xFF + 0x01` sets C but not V.
- **Addressing modes are the real instruction set.** Decoding the mode into an
  effective address once (`addr`) lets LDA/STA/ADC/CMP share code. Zero-page
  indexing must wrap inside page zero (`LDA $FF,X` with X=5 reads `$04`), and
  `(zp,X)` pointers wrap there too.
- **Hardware bugs are part of the spec.** `JMP ($02FF)` reads its high byte
  from `$0200`, not `$0300`; games and test ROMs depend on that.
- **The stack is a page and JSR pushes PC-1.** RTS adds 1 back, so the return
  address logic only works if both halves agree.

## Known limitations

- No cycle counting, no unofficial opcodes, no interrupts (BRK just halts).
- No PPU, APU, cartridge/mapper, or ROM loading -- memory is a flat array.
- Decimal mode is ignored, as on the NES's 2A03.
- No page-cross penalties (no timing at all).

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Credit: ["Writing NES Emulator in Rust"](https://bugzmanov.github.io/nes_ebook/)
by bugzmanov.
