# Learning: a Linux debugger in C++ (ptrace, breakpoints, registers, memory)

**Source:** "Writing a Linux Debugger" by Sy Brand (TartanLlama), Parts 1–3: Setup, Breakpoints, Registers and
memory. From the C/C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The series has 10 parts. Parts 4+ add DWARF debug info (source lines, variables, stack unwinding), which needs
`libelfin`, so this covers the **machine-level core** that the early parts build. The tutorial site was blocked
from the build environment, so the code follows the series' outline, not its text. It skips the tutorial's
`linenoise` and `libelfin` dependencies. Instead it has a small ELF symbol-table reader, so you can type
`break main` instead of a raw address.

> Written by the daily-project routine (Claude). Commits are authored as `Claude`, so they are separate
> from the repo owner's own work in the history.

Build: `make` (Linux x86-64, g++ with C++17, no libraries).
Run: `./build/minidbg build/target`, then `break add`, `continue`, `reg read rdi`, `x add 8`, `stepi`, `help`.
Test: `make test`. 13 tests and 81 checks, run against real processes: a PIE and a non-PIE build of
`tests/target.c`, plus a program that segfaults. The tests also pass under `-fsanitize=address,undefined`.

## Layout

- `src/debugger.cpp`: fork/exec under `PTRACE_TRACEME`, the continue / step / wait loop, stepping over
  breakpoints, signal handling, finding the load address, and the command interpreter.
- `src/breakpoint.cpp`: swaps one byte for `int3` (0xCC) and saves the original so it can be put back.
- `src/registers.cpp`: a table that maps register names to the fields of `user_regs_struct`, with
  `PTRACE_GETREGS` / `SETREGS` to read and write them.
- `src/elf_symbols.cpp`: reads the ELF64 header, section headers and `.symtab`/`.dynsym` to map names to addresses.
- `src/main.cpp`: `minidbg <program> [args...]`.
- `tests/`: the target programs and the test runner (a small `CHECK` macro, no framework).

## What I learned

- **A debugger is just a parent process with ptrace rights.** The child calls `PTRACE_TRACEME` and then `execv`.
  The kernel stops it with `SIGTRAP` before the new program's first instruction. From then on, every signal the
  child gets stops it, and `waitpid` in the parent sees the stop. The whole debugger is a loop of "resume, wait,
  inspect".
- **A software breakpoint is a one-byte self-modifying patch.** `int3` is the single byte `0xCC`, so it fits over
  the first byte of any instruction. `PTRACE_PEEKDATA`/`POKEDATA` work on whole 8-byte words, so you read the word,
  swap only the low byte (x86 is little-endian, so the low byte is the one at the address), write it back, and save
  the original byte.
- **After a breakpoint fires, `rip` is one byte too far.** The CPU executes the `int3` and only then traps, so
  `rip = addr + 1`. Continuing from there would start in the middle of an instruction. You have to set `rip` back
  to `addr`.
- **Continuing past a breakpoint takes four steps.** Restore the original byte, `PTRACE_SINGLESTEP` exactly one
  instruction, put the `0xCC` back, then `PTRACE_CONT`. If you skip the re-arm, the breakpoint only fires once. If
  you skip the single-step, you trap on the same `int3` forever. The test that hits `add` three times and still gets
  the right answer (6) checks all of this.
- **`PEEKDATA` returning -1 doesn't mean it failed.** The return value *is* the data, and `0xffffffffffffffff` is a
  normal word. The only reliable check is to set `errno = 0` before the call and look at it afterwards.
- **A debugger shows memory as the program sees it, not as it actually is.** With a breakpoint set, the code bytes
  really are `cc ...`. `read_bytes` puts the saved bytes back over the patches, so `x add 8` shows `f3 0f 1e fa`
  (`endbr64`). The test checks both views.
- **`user_regs_struct` can be used like an array.** Every field is a `uint64_t`, so a table in the same order as the
  struct lets you look up registers by name. A `static_assert` on the struct size catches a missing entry. Copying
  with `memcpy` instead of casting the struct pointer avoids strict-aliasing undefined behaviour.
- **PIE moves everything.** Modern gcc builds position-independent executables by default, so `nm` shows `add` at
  `0x1149` but at runtime it is at `0x555555555149`. The load base comes from the first mapping of the binary in
  `/proc/<pid>/maps`. The child calls `personality(ADDR_NO_RANDOMIZE)` before `exec`, so the base is the same on
  every run. The tests run on a PIE and a non-PIE build so that both address paths are covered.
- **Calling convention gives you the arguments for free.** When the breakpoint at `add` fires, `rdi` and `rsi` hold
  its arguments (System V AMD64 ABI). Writing `rsi = 100` changed the program's exit code from 6 to 104.
- **Editing memory doesn't rewind the program's logic.** If you write `counter = 40` *inside* `add`, nothing
  changes. The caller saves `add`'s return value into `counter` a moment later, which overwrites the edit. Writing
  it at `main` before the loop runs gives 46. Both cases are tests, because the first one surprised me.
- **Don't swallow the program's own signals.** When the child stops with `SIGSEGV`, the debugger reports it and saves
  the signal number. The next `PTRACE_CONT` passes it back to the child. Otherwise the crash would silently go away
  and the program would execute the faulting instruction again forever. `siginfo.si_code` tells apart a breakpoint
  trap (`SI_KERNEL`/`TRAP_BRKPT`) and a single-step trap (`TRAP_TRACE`).
- **`PTRACE_O_EXITKILL` saves you from orphans.** Without it, a crashed debugger can leave a stopped tracee behind.

## Known limits

- x86-64 Linux only. No source-level debugging (no DWARF), no backtrace, no `step`/`next` over source lines,
  no watchpoints. Parts 4–10 of the series cover those.
- `memory write` stores a whole 8-byte word. Writing over a byte that has a breakpoint in it would overwrite the
  saved `0xCC`.
- Single-threaded tracees only, and it doesn't follow `fork`/`exec` in the program being debugged.
- In this sandbox the REPL was driven from piped input, not an interactive terminal.
