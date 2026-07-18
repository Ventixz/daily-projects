# Write a Shell in C

**Source:** [C/C++ → Write a Shell in C](https://brennan.io/2015/01/16/write-a-shell-in-c/), from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting rather than scaffolded-then-attempted, so this folder
contains the finished implementation directly at the project root (no separate `reference/`).
I went past the tutorial's own scope (which stops at one command at a time) and added pipes and
I/O redirection, since those are what make a shell feel like a shell.

## What it is

A small interactive Unix shell (`myshell`) written against the POSIX C library. It prints a
prompt with the current directory, reads a line, and either runs a built-in (`cd`, `pwd`, `help`,
`exit`) in-process or forks and `execvp`s an external command. It also understands pipelines
(`cmd1 | cmd2 | cmd3`) and redirection (`<`, `>`, `>>`), so `ls | grep .c > matches.txt` works
the way you'd expect.

## Run it

```bash
# Linux / WSL / macOS — gcc and make are typically already present
cd 2026-07-18-c-shell
make
./myshell
```

```powershell
# Windows 11 / PowerShell — this project uses fork()/execvp()/pipe(), which
# have no native Win32 equivalent, so it needs a POSIX layer: WSL (simplest)
# or MSYS2.
wsl --install                      # if WSL isn't already set up
wsl gcc --version                  # confirm a toolchain is present inside WSL
wsl                                 # drop into the Linux environment, then:
cd /mnt/c/path/to/2026-07-18-c-shell
make && ./myshell
```

Try it once running:

```
myshell:/home/user$ echo hi there
hi there
myshell:/home/user$ ls | grep src
src
myshell:/home/user$ echo redirected > out.txt
myshell:/home/user$ cat out.txt
redirected
myshell:/home/user$ cd /tmp
myshell:/tmp$ exit 3
```

Run the smoke tests with `make test` (builds first, then runs `tests/run_tests.sh`).

## What it actually teaches

- **The fork/exec/wait model is the whole story of "running a program."** There's no magic
  syscall for "run this command" — you `fork()` to clone the current process, the child replaces
  itself with the target program via `execvp()` (which only returns if it failed), and the parent
  `waitpid()`s for it to finish. Every shell, every `subprocess.run`, every `os.system` bottoms out
  in this same three-call sequence.
- **Builtins have to run in the shell's own process, not a fork.** `cd` calling `chdir()` inside a
  forked child would change *that child's* working directory and then the child would exit,
  leaving the parent shell exactly where it started. That's why `sh_run_pipeline` special-cases a
  lone builtin to call it directly rather than forking — the same reason real shells implement
  `cd` as a builtin instead of a separate `cd` executable.
- **A pipeline is just pipes wired between forked children, in a loop.** `pipe()` hands you a
  connected read/write file descriptor pair; `dup2()` is how a child clones one of those into its
  own stdin or stdout slot before `exec`ing. The subtlety that actually bites: the parent shell
  must close its own copies of every pipe fd once a child has them, or the reading end of a pipe
  never sees EOF because the shell process is still holding the write end open in the background.
- **Signal disposition is inherited across fork, and that's exactly what a shell wants.** The
  shell itself ignores `SIGINT` (`signal(SIGINT, SIG_IGN)`) so Ctrl-C doesn't kill your terminal
  session, but every child resets it to `SIG_DFL` before `exec`ing — so Ctrl-C *does* kill whatever
  foreground command is currently running. Getting this backwards (or forgetting the reset in the
  child) is the classic first bug when building a shell.

## What I'd add next (stretch goals I skipped for scope)

- Job control: `&` to background a command, plus `jobs`/`fg`/`bg`.
- Environment variable expansion (`$HOME`, `$?`) and `~` expansion in arguments.
- Glob expansion (`*.c`) before exec, the way the shell — not the program — is supposed to do it.
