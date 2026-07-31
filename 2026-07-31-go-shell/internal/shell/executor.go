package shell

import (
	"fmt"
	"io"
	"os"
	"os/exec"
)

// Executor runs a parsed Pipeline against a given set of standard streams.
// It exists (rather than hardcoding os.Stdin/Stdout/Stderr) so tests can
// swap in buffers instead of touching the real terminal.
type Executor struct {
	Stdin  io.Reader
	Stdout io.Writer
	Stderr io.Writer
}

func NewExecutor() *Executor {
	return &Executor{Stdin: os.Stdin, Stdout: os.Stdout, Stderr: os.Stderr}
}

// resolveStreams opens any file redirects for one command and returns the
// stdin/stdout it should actually run with, layered on top of the
// pipeline-position defaults (in/out, or a neighboring pipe end). A later
// redirect of the same direction overrides an earlier one, and any redirect
// overrides the positional default — both match how a real shell resolves
// fd 0/1 for a command.
func resolveStreams(cmd *Command, defaultIn io.Reader, defaultOut io.Writer) (in io.Reader, out io.Writer, closers []io.Closer, err error) {
	in, out = defaultIn, defaultOut
	for _, r := range cmd.Redirects {
		switch r.Kind {
		case RedirIn:
			f, ferr := os.Open(r.Target)
			if ferr != nil {
				closeAll(closers)
				return nil, nil, nil, fmt.Errorf("%s: %w", r.Target, ferr)
			}
			in = f
			closers = append(closers, f)
		case RedirOut:
			f, ferr := os.OpenFile(r.Target, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0644)
			if ferr != nil {
				closeAll(closers)
				return nil, nil, nil, fmt.Errorf("%s: %w", r.Target, ferr)
			}
			out = f
			closers = append(closers, f)
		case RedirAppend:
			f, ferr := os.OpenFile(r.Target, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0644)
			if ferr != nil {
				closeAll(closers)
				return nil, nil, nil, fmt.Errorf("%s: %w", r.Target, ferr)
			}
			out = f
			closers = append(closers, f)
		}
	}
	return in, out, closers, nil
}

func closeAll(closers []io.Closer) {
	for _, c := range closers {
		c.Close()
	}
}

// Run executes a pipeline and returns the exit code of its last stage. A
// single builtin runs in-process; anything else (and every multi-stage
// pipeline) is exec'd as real subprocesses wired together with os.Pipe,
// the same plumbing a C shell builds with pipe(2)/dup2(2)/fork(2) — Go just
// gives us pipe() and fork+exec fused into one exec.Cmd.Start call instead
// of separate syscalls.
func (ex *Executor) Run(p *Pipeline) (int, error) {
	if len(p.Commands) == 0 {
		return 0, nil
	}

	if len(p.Commands) == 1 && IsBuiltin(p.Commands[0].Args[0]) {
		cmd := p.Commands[0]
		in, out, closers, err := resolveStreams(cmd, ex.Stdin, ex.Stdout)
		defer closeAll(closers)
		if err != nil {
			fmt.Fprintln(ex.Stderr, err)
			return 1, nil
		}
		code, err := builtins[cmd.Args[0]](cmd.Args, in, out, ex.Stderr)
		if err != nil {
			return code, err
		}
		return code, nil
	}

	return ex.runExternalPipeline(p)
}

func (ex *Executor) runExternalPipeline(p *Pipeline) (int, error) {
	n := len(p.Commands)
	cmds := make([]*exec.Cmd, n)
	var redirectClosers []io.Closer

	// One real OS pipe per junction between stages, exactly like a C shell's
	// pipe(2) calls before it forks. Passing the *os.File ends straight
	// through as cmd.Stdin/cmd.Stdout makes exec/os hand them to the child
	// as fd 0/1 directly (the Go equivalent of dup2) — no goroutine of ours
	// copies bytes between stages; the kernel does it, same as a real shell.
	type pipeEnds struct{ r, w *os.File }
	pipes := make([]pipeEnds, n-1)
	var pipeFiles []*os.File
	for i := range pipes {
		r, w, err := os.Pipe()
		if err != nil {
			return 1, fmt.Errorf("pipe: %w", err)
		}
		pipes[i] = pipeEnds{r, w}
		pipeFiles = append(pipeFiles, r, w)
	}

	for i, c := range p.Commands {
		if _, err := exec.LookPath(c.Args[0]); err != nil {
			closeFiles(pipeFiles)
			closeAll(redirectClosers)
			fmt.Fprintf(ex.Stderr, "gosh: command not found: %s\n", c.Args[0])
			return 127, nil
		}

		var defaultIn io.Reader = ex.Stdin
		if i > 0 {
			defaultIn = pipes[i-1].r
		}
		var defaultOut io.Writer = ex.Stdout
		if i < n-1 {
			defaultOut = pipes[i].w
		}

		in, out, closers, err := resolveStreams(c, defaultIn, defaultOut)
		redirectClosers = append(redirectClosers, closers...)
		if err != nil {
			closeFiles(pipeFiles)
			closeAll(redirectClosers)
			fmt.Fprintln(ex.Stderr, err)
			return 1, nil
		}

		cmd := exec.Command(c.Args[0], c.Args[1:]...)
		cmd.Stdin = in
		cmd.Stdout = out
		cmd.Stderr = ex.Stderr
		cmds[i] = cmd
	}

	for i, cmd := range cmds {
		if err := cmd.Start(); err != nil {
			for _, started := range cmds[:i] {
				started.Process.Kill()
			}
			closeFiles(pipeFiles)
			closeAll(redirectClosers)
			fmt.Fprintf(ex.Stderr, "gosh: %s\n", err)
			return 1, nil
		}
	}

	// The parent's copies of every pipe fd must close now that each child
	// has its own dup'd copy — otherwise a reader downstream never sees EOF,
	// because the write end the *parent* still held open would keep the
	// pipe alive even after the real producer process exits.
	closeFiles(pipeFiles)

	var waitErr error
	code := 0
	for i, cmd := range cmds {
		err := cmd.Wait()
		if i == n-1 {
			if exitErr, ok := err.(*exec.ExitError); ok {
				code = exitErr.ExitCode()
			} else if err != nil {
				waitErr = err
			}
		}
	}
	closeAll(redirectClosers)
	if waitErr != nil {
		fmt.Fprintf(ex.Stderr, "gosh: %s\n", waitErr)
		return 1, nil
	}
	return code, nil
}

func closeFiles(files []*os.File) {
	for _, f := range files {
		f.Close()
	}
}
