# Writing a Game Boy Emulator (OCaml)

**Source:** ["Writing a Game Boy Emulator in OCaml"](https://linoscope.github.io/writing-a-game-boy-emulator-in-ocaml/)
by linoscope, from the OCaml section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

## What to build

The Sharp SM83 CPU at the heart of the original DMG Game Boy: the full
instruction set (all documented opcodes, `0x00`-`0xFF`, plus the full
`0xCB`-prefixed bit-op table), register/flag semantics, and the memory bus
it reads and writes through.

Out of scope for a one-day build, and fine to skip: the PPU (no rendering),
the APU (no sound), timers, interrupt *servicing* (IME/EI/DI can still be
tracked), and MBC bank switching (treat the address space as a flat 64KB
image — real cartridges bank-switch ROM/RAM above 32KB, which only matters
once a program is bigger than that). None of those change what the CPU
core itself has to get right.

## What it teaches

- How a real CPU's opcode encoding is *structured*, not arbitrary: opcodes
  group by shared bit fields (`01 ddd sss` for every 8-bit register-to-register
  load, `10 ooo sss` for every ALU op against `A`), so a decoder can dispatch
  on bit patterns instead of hand-writing hundreds of cases.
- Flag semantics that aren't obvious from a spec skim: half-carry is
  computed on the low nibble *before* the operation, `INC`/`DEC` deliberately
  leave the carry flag alone, and `DAA`'s BCD-correction table has to inspect
  the flags left by the *previous* instruction.
- A well-known trick for getting output out of a CPU-only emulator with no
  display: many real Game Boy test ROMs (Blargg's suite) report pass/fail
  by writing to the serial port registers (`SB`/`SC` at `0xFF01`/`0xFF02`)
  instead of drawing anything, because a link cable doesn't need a screen.

## Setup

- No emulator-specific dependencies — OCaml's standard library is enough for
  a byte array, a register file, and a big pattern match.
- A disassembler/opcode-table reference (e.g. the [Pan Docs](https://gbdev.io/pandocs/)
  opcode tables, or [gbops](https://izik1.github.io/gbops/)) is worth having
  open throughout — this is the part of the exercise where getting a flag
  formula subtly wrong is easy and only shows up as a test ROM that hangs.

## Milestones

1. Registers, flags, and a flat memory bus.
2. 8-bit loads, then the ALU op group (`ADD`/`ADC`/`SUB`/`SBC`/`AND`/`XOR`/`OR`/`CP`).
3. 16-bit loads, `INC`/`DEC rr`, `ADD HL,rr`, and the stack (`PUSH`/`POP`).
4. Control flow: `JP`/`JR`/`CALL`/`RET`/`RST`, all four with their conditional
   forms.
5. The `0xCB`-prefixed table: rotates/shifts, `BIT`/`RES`/`SET`.
6. `DAA` and the `SP`+signed-immediate flag quirk (`ADD SP,e8` / `LD HL,SP+e8`)
   — the two corners of the ISA every port gets wrong on the first try.
7. A test program (no assembler exists for a from-scratch CPU, so hand-encode
   one) that exercises loads, arithmetic, a loop, and a subroutine call, and
   prints its result over the serial port.
