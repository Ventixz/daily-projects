package main

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
)

const usage = `usage: minicontainer run --root DIR [--hostname NAME] [--pids N] -- CMD [ARGS...]
(needs root on Linux; DIR must contain the binaries CMD needs, see make-rootfs.sh)`

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, usage)
		os.Exit(2)
	}
	o, err := ParseArgs(os.Args[2:])
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		fmt.Fprintln(os.Stderr, usage)
		os.Exit(2)
	}
	switch os.Args[1] {
	case "run":
		err = runParent(o)
	case "child":
		err = runChild(o)
	default:
		fmt.Fprintln(os.Stderr, usage)
		os.Exit(2)
	}
	var ee *exec.ExitError
	if errors.As(err, &ee) {
		os.Exit(ee.ExitCode())
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}
