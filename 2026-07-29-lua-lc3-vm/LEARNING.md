# LC-3 Virtual Machine (Lua)

**Source:** [Write your own Virtual Machine](https://www.jmeiners.com/lc3-vm/) (justinmeiners/lc3-vm),
from [practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

## What it is

An emulator for the LC-3, a 16-bit teaching ISA: 8 general registers, a program counter, a
3-value condition-code register (negative/zero/positive), and a 64K-word address space.
`src/vm.lua` is the CPU — one `step()` that fetches a word, decodes its top 4 bits into an
opcode, and executes it, with zero I/O inside it beyond an injectable `vm.io` table. `src/asm.lua`
is a real two-pass assembler for a subset of LC-3 asm (`ADD`/`AND`/`NOT`, all the load/store forms,
`BR`/`JMP`/`JSR`, `TRAP` and its GETC/OUT/PUTS/IN/PUTSP/HALT aliases, `.ORIG`/`.FILL`/`.BLKW`/
`.STRINGZ`). `bin/lc3as.lua` and `bin/lc3.lua` are the CLI front ends — assemble a `.asm` file to a
real LC-3 `.obj` binary, then load and run that binary.

## Run it

```bash
cd 2026-07-29-lua-lc3-vm
lua5.4 tests/run_all.lua                                    # 25 assertions, no LuaRocks/busted needed

lua5.4 bin/lc3as.lua programs/hello.asm programs/hello.obj  # assemble
lua5.4 bin/lc3.lua programs/hello.obj                       # → Hello, LC-3!

lua5.4 bin/lc3as.lua programs/sum10.asm programs/sum10.obj  # a real loop: sums 1..10 into R1
lua5.4 bin/lc3.lua programs/sum10.obj                       # halts silently; see test_asm.lua for the register check
```

## What it actually teaches

- **A CPU's instruction set is a `switch` on a 4-bit opcode, and every addressing mode is just
  where the offset gets added.** `LD`, `LDI`, `LEA`, `ST`, `STI` all share the exact same PCoffset9
  field layout — the only thing that differs between "compute an address" (`LEA`), "load through
  it" (`LD`), and "load through a pointer stored at it" (`LDI`) is one extra `mem_read` in between.
  Seeing all five side by side in `vm.lua` makes clear that "addressing mode" isn't a deep concept,
  it's a naming convention for how many indirections happen after the same arithmetic.
- **A PC-relative offset is relative to the PC *after* the fetch, not before.** Every offset
  calculation in the assembler (`pcoffset` in `asm.lua`) subtracts `addr + 1`, not `addr`, from the
  target — because by the time an instruction executes, `step()` has already advanced `vm.pc` past
  it. Get this off-by-one wrong in either the assembler or the VM and every branch and `LD`/`ST`
  lands one word away from where the source says it should, which is exactly the kind of bug that
  only shows up as "the program runs but does the wrong thing," not a crash.
- **A two-pass assembler exists specifically so labels can point forward.** `BR LOOP_END` written
  before `LOOP_END` is defined can't be encoded in one pass — the offset depends on an address the
  assembler hasn't seen yet. Pass 1 in `asm.lua` walks the whole source purely to build the
  `label -> address` table (tracking how many words each `.FILL`/`.BLKW`/`.STRINGZ`/instruction
  takes, without encoding anything); pass 2 then encodes every instruction with every label already
  resolved, so a forward reference and a backward reference cost exactly the same.
- **`VF`-style dual-purpose registers show up here too, just with condition codes instead of a flag
  register.** Every instruction that writes a register (`ADD`, `AND`, `NOT`, `LD`, `LDR`, `LEA`, ...)
  also overwrites the *shared* N/Z/P condition register, and `BR` reads whatever was set by
  whichever instruction ran most recently — so a `CMP`-then-`BR` pattern (common in real ISAs)
  becomes, here, "do an `ADD`/`AND` you don't otherwise care about, then branch on its flags,"
  because there's no separate compare instruction at all.
- **`TRAP` is a software interrupt with no OS underneath it, on purpose.** A real LC-3 loads actual
  trap-routine assembly into low memory and jumps to it; this emulator (matching the reference
  tutorial's scope) implements `GETC`/`OUT`/`PUTS`/`IN`/`PUTSP`/`HALT` directly in `vm.lua` as Lua
  functions instead. That's a real simplification — programs can't inspect or override trap
  behavior — but it's the difference that lets `tests/test_vm.lua` swap in a mock `vm.io` and assert
  on captured output instead of needing a real terminal, the same separation `2026-07-28-cpp-chip8-emulator`
  used to keep its CPU tests off a TTY.
- **The "binary format" of a 16-bit VM is nothing more than a length-prefixed array of big-endian
  words.** `bin/lc3as.lua` writes the load address as the first word, then every program word after
  it, high byte first — and `bin/lc3.lua` reads exactly that back. There's no header magic number,
  no section table, nothing — which is what makes it worth seeing once: most "binary formats" people
  find intimidating are this same idea with more fields.

## What I'd add next (stretch goals I skipped for scope)

- Memory-mapped I/O (`KBSR`/`KBDR`) so a running program can poll for a keypress without blocking,
  instead of relying only on the blocking `GETC`/`IN` traps.
- A disassembler (the inverse of `asm.lua`'s `encode`) to turn a `.obj` back into readable assembly
  — useful for verifying a hand-written program did what the source claims.
- Friendlier assembler diagnostics: line numbers and a real error type instead of `error()` with a
  bare message, so a typo'd register or unresolved label points at the offending line.
- `.NEG`/character-literal operands and case-insensitive mnemonics in the assembler's tokenizer —
  it currently expects the exact syntax used in `programs/*.asm`, not the full breadth of real LC-3
  assembly source found in the wild.
