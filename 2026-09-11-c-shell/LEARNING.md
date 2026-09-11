# Write a Shell in C

**Source:** ["Write a Shell in C"](https://brennan.io/2015/01/16/write-a-shell-in-c/) by
Stephen Brennan, one of the C/C++ entries in
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting. No dependencies beyond the C
standard library and POSIX (`fork`, `execvp`, `pipe`, `dup2`, `waitpid`).

## What it is

A real (if small) Unix shell, split so each concern lives in one file:

- `src/tokenizer.c` — reads a line with `getline` and splits it into
  tokens, honoring `'single'` and `"double"` quotes as a single token each.
- `src/parser.c` — turns the flat token list into a *pipeline*: one or more
  commands separated by `|`, each with its own optional `<file`, `>file`,
  or `>>file` redirection.
- `src/executor.c` — the actual point of the project: wires N commands
  together with `pipe()`/`dup2()`/`fork()`, or runs a lone builtin directly
  in the shell's own process when there's nothing to pipe.
- `src/builtins.c` — `cd`, `pwd`, `help`, `exit`.
- `src/shell.c` — the read-parse-execute loop.

## Run it

```bash
cd 2026-09-11-c-shell
make
make test                    # 12 tests, no framework, just diffed output

./shell
shell:/home/you/daily-projects/2026-09-11-c-shell$ echo hi | tr a-z A-Z
HI
shell:/home/you/daily-projects/2026-09-11-c-shell$ ls *.md > /tmp/listing.txt
shell:/home/you/daily-projects/2026-09-11-c-shell$ cat /tmp/listing.txt
LEARNING.md
shell:/home/you/daily-projects/2026-09-11-c-shell$ cd /tmp && pwd
/tmp
shell:/tmp$ exit
```

## What it actually teaches

- **A shell is just fork + exec + wait, over and over.** The REPL loop
  itself is almost embarrassingly small — read a line, split it into
  argv, fork a child, `execvp` replaces that child's process image, the
  parent `waitpid`s. Everything else in this project (builtins, pipes,
  redirection) is really about deciding *what file descriptors the child
  inherits* before that `execvp` call, not about the loop itself.

- **A builtin only matters if it runs in the parent.** `cd` changes the
  calling process's working directory — if you fork before running it,
  you've just changed a child's cwd and the child exits immediately,
  so the shell never moves. That's why `sh_execute_pipeline` special-cases
  a lone builtin with no redirection: it calls `sh_builtin_run` directly,
  no `fork()` at all. The same builtin invoked as one stage of a longer
  pipeline (`cd /tmp | true`) *does* fork like everything else, which
  matches how real shells behave — `cd` inside a pipeline is a no-op on
  the parent for the same reason.

- **Buffered stdio and raw file descriptors don't share a queue.** The
  first version of this shell printed builtin output (`pwd`, `help`) in
  the wrong place whenever stdout wasn't a terminal (i.e. exactly the
  case the test suite exercises, and exactly the case that matters for
  scripting). `printf` in the parent process buffers into libc's stdio
  buffer; a forked child's `execvp`'d command writes straight to fd 1 with
  no buffering in between. Without an `fflush` before the next `fork()`,
  a `pwd` followed by `echo marker` could print `marker` first and the cwd
  second, even though `pwd` ran first — the bytes were sitting in a buffer
  the child had no way to know about. Fixed by flushing stdout right after
  a builtin prints, and again with `fflush(NULL)` immediately before every
  `fork()` in the pipeline as a general guard.

- **N-stage pipelines are one loop with a "previous read end" variable,
  not N-1 special cases.** Each iteration only needs to know the read end
  of the *previous* stage's pipe (or `NULL` for the first stage) and
  whether there's a *next* stage to open a new pipe for. Redirection files
  (`<`, `>`, `>>`) only apply to the first and last stage respectively —
  everything in between is wired pipe-to-pipe. Getting the `close()` calls
  right in both parent and child (each end of each pipe must be closed
  everywhere it isn't actually used) is what turns "works once" into
  "doesn't deadlock or leak fds on the fifth pipeline you run."

## Known limitations (by design, to keep scope to ~2-4 hours)

- Operators (`|`, `<`, `>`, `>>`) must be space-separated from their
  neighbors — `echo hi>file` isn't recognized, `echo hi > file` is.
- No `&&`, `||`, `;`, background jobs (`&`), globbing, or environment
  variable expansion (`$VAR`). Real shells layer these on top of exactly
  the fork/exec/pipe core this project builds.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Credit: ["Write a Shell in C"](https://brennan.io/2015/01/16/write-a-shell-in-c/) by Stephen Brennan.
