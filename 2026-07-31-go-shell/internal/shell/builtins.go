package shell

import (
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
)

// ExitRequest is returned (wrapped) by the `exit` builtin. The REPL checks
// for it with errors.As to know when to stop reading input, as opposed to a
// genuine execution error.
type ExitRequest struct {
	Code int
}

func (e *ExitRequest) Error() string {
	return fmt.Sprintf("exit requested with code %d", e.Code)
}

// builtin is a shell command implemented in-process rather than by exec-ing
// an external program. Builtins exist for anything that must mutate the
// shell's own state (cd changes *this* process's cwd; an external `cd`
// binary could only ever change its own, and would be useless) or that's
// simple enough not to need a subprocess.
type builtin func(args []string, stdin io.Reader, stdout, stderr io.Writer) (int, error)

var builtins map[string]builtin

func init() {
	builtins = map[string]builtin{
		"cd":     biCd,
		"pwd":    biPwd,
		"echo":   biEcho,
		"exit":   biExit,
		"export": biExport,
		"unset":  biUnset,
		"help":   biHelp,
	}
}

// IsBuiltin reports whether name is handled in-process instead of exec'd.
func IsBuiltin(name string) bool {
	_, ok := builtins[name]
	return ok
}

func biCd(args []string, _ io.Reader, _, stderr io.Writer) (int, error) {
	dir := ""
	if len(args) > 1 {
		dir = args[1]
	} else {
		dir = os.Getenv("HOME")
		if dir == "" {
			fmt.Fprintln(stderr, "cd: HOME not set")
			return 1, nil
		}
	}
	if err := os.Chdir(dir); err != nil {
		fmt.Fprintf(stderr, "cd: %s\n", err)
		return 1, nil
	}
	if wd, err := os.Getwd(); err == nil {
		os.Setenv("PWD", wd)
	}
	return 0, nil
}

func biPwd(_ []string, _ io.Reader, stdout, stderr io.Writer) (int, error) {
	wd, err := os.Getwd()
	if err != nil {
		fmt.Fprintf(stderr, "pwd: %s\n", err)
		return 1, nil
	}
	fmt.Fprintln(stdout, wd)
	return 0, nil
}

func biEcho(args []string, _ io.Reader, stdout, _ io.Writer) (int, error) {
	rest := args[1:]
	newline := true
	if len(rest) > 0 && rest[0] == "-n" {
		newline = false
		rest = rest[1:]
	}
	fmt.Fprint(stdout, strings.Join(rest, " "))
	if newline {
		fmt.Fprintln(stdout)
	}
	return 0, nil
}

func biExit(args []string, _ io.Reader, _, stderr io.Writer) (int, error) {
	code := 0
	if len(args) > 1 {
		n, err := strconv.Atoi(args[1])
		if err != nil {
			fmt.Fprintf(stderr, "exit: numeric argument required: %s\n", args[1])
			return 2, nil
		}
		code = n
	}
	return code, &ExitRequest{Code: code}
}

func biExport(args []string, _ io.Reader, _, stderr io.Writer) (int, error) {
	for _, kv := range args[1:] {
		name, value, found := strings.Cut(kv, "=")
		if !found {
			fmt.Fprintf(stderr, "export: invalid assignment: %s\n", kv)
			return 1, nil
		}
		os.Setenv(name, value)
	}
	return 0, nil
}

func biUnset(args []string, _ io.Reader, _, stderr io.Writer) (int, error) {
	for _, name := range args[1:] {
		os.Unsetenv(name)
	}
	return 0, nil
}

func biHelp(_ []string, _ io.Reader, stdout, _ io.Writer) (int, error) {
	fmt.Fprintln(stdout, "gosh builtins: cd pwd echo export unset exit help")
	fmt.Fprintln(stdout, "anything else is looked up on PATH and run as a subprocess")
	return 0, nil
}
