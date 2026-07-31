# gosh — a Unix Shell (Go)

**Source:** [Tutorial - Write a Shell in C](https://brennan.io/2015/01/16/write-a-shell-in-c/) by
Stephen Brennan, from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning)
(C/C++ section). Built in Go instead of C — same shape (read, tokenize, parse, execute), different
tools for connecting processes: no raw `fork()`/`dup2()` available, so `os/exec` plus `os.Pipe`
stand in for them, and I document below where that substitution changes what's actually happening.

## What it is

A small interactive shell, `gosh`, split into a REPL (`main.go`) and a library package
(`internal/shell`) with three stages:

- `lexer.go` — `Tokenize` turns a line into `Word`/`Pipe`/`RedirectIn`/`RedirectOut`/
  `RedirectAppend` tokens, handling single quotes (fully literal), double quotes (literal except
  `\"`, `\\`, `\$`), a bare backslash escape outside quotes, and operators that split a word even
  without surrounding whitespace (`out>file` lexes as three tokens, the way a real shell reads it).
- `parser.go` — `Parse` turns tokens into a `Pipeline` of `Command`s, each with its own `Args` and
  `[]Redirect`. Rejects the token shapes that can't mean anything: a leading/trailing/doubled `|`,
  or a redirect operator with nothing after it.
- `executor.go` — `Executor.Run` executes a `Pipeline`. A lone builtin (`cd`, `pwd`, `echo`,
  `export`, `unset`, `exit`, `help`) runs in-process; everything else is exec'd as a real subprocess,
  wired to its neighbors with `os.Pipe`.

## Run it

```bash
cd 2026-07-31-go-shell
go test ./...          # lexer, parser, and executor tests — the executor tests spawn real
                        # subprocesses (sh, cat, sort, head, grep) and check actual pipe/redirect behavior

go run .                # interactive
gosh:/some/path$ echo hello | tr a-z A-Z
HELLO
gosh:/some/path$ printf 'b\na\n' | sort > sorted.txt
gosh:/some/path$ cat sorted.txt
a
b
gosh:/some/path$ exit 0
```

## What it actually teaches

- **A shell is three separable stages, and keeping them separable is what makes the whole thing
  testable.** Tokenize → Parse → Run is the same read-eval-print split Brennan's C version uses
  (`sl_read_line` / `sl_split_line` / `sl_execute`), but here each stage is an exported function
  returning plain data (`[]Token`, then `*Pipeline`), so `lexer_test.go` and `parser_test.go` never
  spawn a process at all — they just assert on token/AST shapes. Only `executor_test.go` needs real
  subprocesses. Splitting "parse the sentence" from "act on the sentence" is the same shape as
  `2026-07-26-java-lox-interpreter`'s scanner/parser/interpreter split; a shell's grammar is just
  much smaller.
- **A pipeline's file descriptors are set up before any process exists, then handed off.** In C
  you `pipe()`, then `fork()` twice, then in each child `dup2()` the right end onto fd 0 or 1 and
  close every fd the child doesn't need, then `exec()`. `executor.go`'s `runExternalPipeline` does
  the same three things in the same order — `os.Pipe()` for every junction between stages before
  starting anything, `cmd.Stdin`/`cmd.Stdout` assignment to route each `*os.File` end (which
  `exec.Cmd.Start` hands to the child as fd 0/1, Go's answer to `dup2`), then `Start()`. The one
  real substitution: Go has no bare `fork()` (unsafe with goroutines/the runtime's own threads), so
  `Start()` fuses fork+exec into one call instead of leaving a window to fix up fds between them —
  which is also why the redirect-vs-pipe layering (see next point) has to happen before `Start()`
  rather than in a post-fork child-side step.
- **The parent must close its own copies of every pipe fd once children are started, or EOF never
  arrives.** `os.Pipe()` gives the *parent* both ends; after each child has its own dup'd copy via
  `Start()`, the parent's copies are just extra open references keeping the pipe alive. Skip closing
  them and a downstream reader (e.g. `head`'s stdin) blocks forever, because the write end the parent
  still holds open never signals EOF even after the real producer (`sort`) has exited. This is exactly
  why C shells close both ends of every pipe in the parent right after `fork()` — `closeFiles(pipeFiles)`
  right after the `Start()` loop is the Go version of that same close.
- **Redirects and pipe position are two independent things that both want to set fd 0/1, so one has
  to explicitly win.** `resolveStreams` computes the pipeline-position default (previous stage's pipe,
  or the shell's own stdin/stdout at the ends) first, then lets any `Redirect` on that command
  override it. That's not an arbitrary implementation choice — it matches real shells, where
  `cmd1 > file | cmd2` really does send `cmd1`'s output to `file`, not into the pipe, because
  redirect setup happens after pipe setup in the fork/exec sequence either way.
- **A builtin exists only when a subprocess *couldn't* do the job.** `cd` is the textbook case: an
  external `cd` binary could only ever `chdir()` its own process, which exits immediately and leaves
  the shell's cwd untouched — so `cd` has to run in the shell's own process, which is exactly what
  `Run` special-cases (a lone builtin skips `exec.Command` entirely and calls the Go function
  directly against `resolveStreams`'s in/out). `echo`, `export`, `pwd` didn't strictly need to be
  builtins (real `/bin/echo` exists), but making them builtins is what let `TestExportAffectsSubprocessEnv`
  assert that `export FOO=bar` — which calls `os.Setenv`, changing *this* process's environment — is
  visible to a subsequently exec'd `sh -c 'echo $FOO'`, without needing a real login shell's semantics.

## What I'd add next (stretch goals I skipped for scope)

- Job control (`&`, `fg`, `bg`, `Ctrl-Z`) — needs process groups and signal handling, which is a
  second whole subsystem on top of what's here.
- `$VAR` and `$(...)` expansion in words — the double-quote lexer already reserves `\$` as an escape
  in anticipation of this, but no expansion pass exists yet.
- `&&` / `||` / `;` command sequencing — right now every line is exactly one pipeline.
- Builtins inside a pipeline (`cd /tmp | echo hi` currently just execs a `cd` binary that doesn't
  exist and reports "command not found"). Doable, but means giving every builtin the same
  goroutine-and-pipe treatment external commands get, for a case that's rare in practice.
