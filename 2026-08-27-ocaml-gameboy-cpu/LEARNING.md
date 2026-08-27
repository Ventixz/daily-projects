# Writing a Game Boy Emulator (OCaml)

**Source:** ["Writing a Game Boy Emulator in OCaml"](https://linoscope.github.io/writing-a-game-boy-emulator-in-ocaml/)
by linoscope, from the OCaml section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Zero external libraries — no opam packages were installed for this, so
everything (including the test harness) is built on the standard library
alone.

## What it is

A complete Sharp SM83 CPU core: every documented opcode, `0x00`-`0xFF` plus
the full `0xCB`-prefixed table, decoded, executed, and flag-accurate. What's
deliberately *not* here is the rest of the console around that CPU — no
PPU, no APU, no timers, no interrupt servicing, no MBC bank switching. Those
are peripheral concerns; the CPU's instruction set is complete regardless
of what's wired up around it, the same way this repo's browser engine
implemented a real layout algorithm without needing a JS engine attached to
it.

- `src/memory.ml` — a flat 64KB byte array. Real hardware splits this
  address space across ROM banks, VRAM, work RAM, OAM, I/O registers and
  HRAM, with an MBC chip switching ROM banks in and out above `0x8000`. None
  of that matters for a hand-written test program that fits in 32KB and
  never needs a second bank, so it's just bytes. The one piece of real
  hardware behavior it *does* implement: writing to `0xFF02` (the serial
  transfer-control register) with its start bit (`0x80`) set immediately
  "transmits" whatever byte sits in `0xFF01` by appending it to a buffer —
  this is exactly how headless Game Boy test ROMs (Blargg's suite) report
  output with no display attached.
- `src/cpu.ml` — registers (`a`,`b`,`c`,`d`,`e`,`h`,`l`,`f`,`sp`,`pc`), the
  four CPU flags packed into `f`'s top nibble, and `step`, which fetches one
  opcode and executes it. The decoder is organized the way the real SM83
  opcode map is laid out, not as 500 individual `match` arms:
  - `01 ddd sss` (`0x40`-`0x7F` minus `0x76`) is every 8-bit `LD r,r'` —
    `dst`/`src` are pulled straight out of the opcode's bit fields via one
    `get_r8`/`set_r8` pair indexed 0-7 as B,C,D,E,H,L,(HL),A.
  - `10 ooo sss` (`0x80`-`0xBF`) is the whole ALU group against `A` —
    `ADD`/`ADC`/`SUB`/`SBC`/`AND`/`XOR`/`OR`/`CP` selected by a 3-bit op
    index, shared with the `A,n` immediate forms (`0xC6`-`0xFE`).
  - The `0xCB` table decodes the same way: bits 7-6 pick rotate/shift vs.
    `BIT`/`RES`/`SET`, bits 5-3 pick the shift variant or bit index, bits
    2-0 pick the register — `exec_cb` handles all 256 `CB`-prefixed opcodes
    with one function instead of a table of 256.
  - Conditional jumps/calls/returns (`JP cc`/`JR cc`/`CALL cc`/`RET cc`) all
    collapse to the same guard (`op land 0xE7`) with the two condition bits
    pulled out separately — one code path covers all sixteen opcodes.
  This isn't a stylistic choice — it's how a real decoder (and every SM83
  disassembler and reference opcode table, e.g. gbops) actually reads the
  encoding, so writing it this way is *less* code than hand-listing cases,
  not a clever compression of it.
- Flag correctness details that don't show up until something is wrong:
  `INC r`/`DEC r` update Z/N/H but leave the carry flag untouched (`SUB`
  updates all four) — a wrong port of this is invisible until a loop
  counter's carry gets silently clobbered. `DAA` inspects the N/H/C flags
  left by the *previous* instruction, not the accumulator's raw value, to
  decide its BCD correction — implemented per the standard correction
  table, verified against the textbook example (`0x15 + 0x27` → `DAA` →
  `0x42`, correct decimal 15+27=42). `ADD SP,e8` and `LD HL,SP+e8` compute
  H/C from an *unsigned* byte add against SP's low byte even though the
  operand is added to SP as a signed displacement — a real hardware quirk,
  not a bug, and it's the one flag rule in this whole opcode table that
  looks wrong until you check Pan Docs twice.
- `src/main.ml` — loads a raw binary image at address 0, runs until `HALT`
  (or a 10-million-instruction safety cap, in case a hand-written test
  program has a bad jump), prints anything the program sent over the
  serial port, then dumps final register state.
- `roms/assemble.ml` — there's no assembler for a from-scratch CPU with no
  existing toolchain, so this is a tiny two-pass assembler for exactly one
  demo program: it resolves `JR`'s signed displacement and `CALL`'s
  absolute address from labels instead of hand-counting bytes, which is
  the one part of hand-assembling machine code that's actually worth
  automating — a single miscounted byte offset sends the CPU jumping into
  the middle of an instruction, and that failure mode doesn't look like an
  off-by-one, it looks like the emulator is broken.
- `roms/fib.gb` (generated, not committed) — the demo program: prints the
  first twelve Fibonacci numbers in hex over the serial port, one per loop
  iteration, via a `CALL`ed subroutine that reuses `SWAP`+`AND` to split a
  byte into nibbles and prints each with a shared `print_nibble` routine.
  Exercises loads, the ALU, a conditional loop (`JR NZ`), and nested `CALL`s
  in one program.
- 44 tests across loads, the ALU (including flag edge cases — half-carry,
  zero-on-wrap, `ADC`/`SBC` carry chaining), `INC`/`DEC` leaving carry
  alone, 16-bit arithmetic, the stack, all four conditional-branch forms,
  `RST`, five `CB`-table operations, `DAA`, the `SP+e8` flag quirk, and the
  serial-port capture convention itself.

## Run it

```bash
cd 2026-08-27-ocaml-gameboy-cpu
make test                      # 44 tests
make run                       # assembles roms/fib.gb, runs it, prints the sequence
```

`make run` prints:

```
01 01 02 03 05 08 0D 15 22 37 59 90
```

— the first twelve Fibonacci numbers (1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89,
144) as hex bytes, followed by the CPU's final register dump.

To run any other hand-assembled program: `make all && ./build/gbcpu path/to.bin`.
