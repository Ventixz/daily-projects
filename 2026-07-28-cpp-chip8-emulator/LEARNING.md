# CHIP-8 Emulator (C++)

**Source:** [Building a CHIP-8 Emulator](https://austinmorlan.com/posts/chip8_emulator/),
from [practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

## What it is

A CHIP-8 interpreter: 4KB of memory, 16 8-bit registers (`V0`-`VF`), a 16-bit index register,
a 16-level call stack, two 60Hz countdown timers, and a 64x32 monochrome display, all driven by
one `cycle()` that fetches a 2-byte instruction, decodes it, and executes it. `chip8::Chip8`
(`include/chip8.hpp`, `src/chip8.cpp`) is pure emulation state with no I/O in it at all — no
files, no terminal, no clock — which is what lets `tests/test_chip8.cpp` drive it one instruction
at a time and assert on exact register/memory/display state after each one. `src/main.cpp` is the
only place that touches the outside world: it reads a ROM file, puts the terminal into raw
non-blocking mode, runs the CPU at a fixed rate, and renders the display buffer as `#`/space
characters over an ANSI cursor-home escape.

## Run it

```bash
cd 2026-07-28-cpp-chip8-emulator
make test                              # 57 assertions, no GoogleTest/Catch2 to install
python3 roms/make_digits_rom.py        # hand-assembles a demo ROM (draws glyphs 0-9)
make run ROM=roms/digits.ch8           # renders it in the terminal; Esc to quit
```

`make run` needs a real terminal (it puts stdin into raw mode) — there's no windowing
dependency, but there is a TTY dependency, which is why the test suite talks to `Chip8`
directly instead of going through a rendered frame.

## What it actually teaches

- **An instruction set is a `switch` on bit-fields you slice out with masks and shifts, not a
  parser.** Every CHIP-8 opcode is exactly 2 bytes, and `execute()` pulls `x`, `y`, `n`, `kk`,
  `nnn` out of it with `& 0xF000`/`>> 8` before ever looking at what the instruction *means*. The
  outer `switch (opcode & 0xF000)` dispatches on the family (8 instructions all start `0x8`),
  and a handful of families (`0x0`, `0x8`, `0xE`, `0xF`) need a second switch on the low nibble or
  low byte because the high nibble alone is ambiguous — `0x8xy4` (ADD) and `0x8xy5` (SUB) only
  differ in their last nibble. This is the same shape as a real ISA decoder, just small enough
  to hold in your head entirely.
- **`VF` is simultaneously a general-purpose register and the CPU's only flag register — every
  arithmetic opcode racing to write it means order matters.** `8xy4` (ADD) sets `VF` to the carry
  *after* computing the sum but *before* possibly using `V[0xF]` as `Vx`; if `x == 0xF`, the flag
  write has to be the last thing that happens, or the carry result stomps over the arithmetic
  result the caller wanted. The tests exercise the carry/borrow/shift cases with plain non-`VF`
  registers to isolate the flag behavior from that edge case, but real ROMs sometimes intentionally
  use `VF` as the destination register precisely to discard the result and keep only the flag.
- **A sprite draw is XOR, which makes "erase" and "collision" the same primitive.** `Dxyn` XORs
  each sprite bit into the display buffer and sets `VF` if any pixel that was already `1` got
  turned to `0`. That single rule gives CHIP-8 two things for free that a naive
  "set pixel" draw wouldn't: drawing the same sprite at the same spot twice erases it exactly
  (used constantly for simple animation — draw, move, draw again), and `VF` becoming `1` is a
  free, hardware-level collision detector games use for hit-testing without any bounding-box math.
- **Blocking on input inside a von Neumann fetch loop means "don't advance the program counter,"
  not "stop the loop."** `Fx0A` (wait for key) can't halt `cycle()` itself — the timers still
  need to tick and the caller still owns the frame loop — so it just decrements `pc_` by 2 right
  after `cycle()` had already advanced it past the instruction, causing the *same* instruction to
  be fetched and re-executed next cycle until a key shows up. It's a busy-wait implemented
  entirely as "don't move," with no separate blocked/running state anywhere in the CPU.
- **A terminal has no key-up event, so "held key" has to be faked, and faking it has a real
  cost.** `main.cpp` reads whatever characters are waiting via `select()` each frame, marks those
  keys pressed for the CPU's cycles that frame, then clears every key before the next poll. A ROM
  that expects `Ex9E`/`ExA1` to see a key stay down across many frames (continuous movement,
  held-fire) will see it as a single flickering tap instead — a genuine behavioral gap, not
  a rendering nicety, and the reason a "real" CHIP-8 frontend needs an actual windowing/input
  library with key-up events rather than a terminal.

## What I'd add next (stretch goals I skipped for scope)

- Persist keys as actually-down/up by pairing this with a proper input backend (SDL2 or similar)
  instead of terminal polling — the one limitation above that terminal I/O genuinely can't fix.
- Super-CHIP (`SCHIP`) extensions: 128x64 hi-res mode, scrolling opcodes, and the extra
  `Fx75`/`Fx85` flag-register-persistence instructions many later ROMs assume.
- Configurable CPU speed (`CYCLES_PER_FRAME` is currently a fixed constant) — real ROMs vary in
  how many instructions per 60Hz frame they were tuned against, and getting it wrong shows up as
  either sluggish or too-fast gameplay.
- Save/restore state (a snapshot of memory + registers + timers) for a rewind/save-state feature,
  which is mostly "serialize the `Chip8` fields" once they're all plain, trivially-copyable POD.
