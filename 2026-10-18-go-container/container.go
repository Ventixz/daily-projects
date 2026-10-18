package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
)

// Options is what the user asked for on the command line.
type Options struct {
	Root     string // directory used as the container's filesystem root
	Hostname string
	PidsMax  int // 0 = unlimited
	Command  []string
}

// ParseArgs parses: run [--root DIR] [--hostname NAME] [--pids N] -- cmd args...
func ParseArgs(args []string) (Options, error) {
	o := Options{Hostname: "container"}
	i := 0
	for ; i < len(args); i++ {
		a := args[i]
		if a == "--" {
			i++
			break
		}
		if !strings.HasPrefix(a, "--") {
			break
		}
		if i+1 >= len(args) {
			return o, fmt.Errorf("flag %s needs a value", a)
		}
		v := args[i+1]
		switch a {
		case "--root":
			o.Root = v
		case "--hostname":
			o.Hostname = v
		case "--pids":
			n, err := strconv.Atoi(v)
			if err != nil || n < 0 {
				return o, fmt.Errorf("--pids must be a non-negative integer, got %q", v)
			}
			o.PidsMax = n
		default:
			return o, fmt.Errorf("unknown flag %s", a)
		}
		i++
	}
	o.Command = args[i:]
	if o.Root == "" {
		return o, fmt.Errorf("--root is required")
	}
	if len(o.Command) == 0 {
		return o, fmt.Errorf("no command given")
	}
	return o, nil
}

// runParent re-executes this binary as "child" inside new UTS, PID and mount
// namespaces. The new PID namespace applies to the child, which is why we
// re-exec instead of just calling unshare in-process.
func runParent(o Options) error {
	args := []string{"child", "--root", o.Root, "--hostname", o.Hostname, "--pids", strconv.Itoa(o.PidsMax), "--"}
	args = append(args, o.Command...)
	cmd := exec.Command("/proc/self/exe", args...)
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr
	cmd.SysProcAttr = &syscall.SysProcAttr{
		Cloneflags: syscall.CLONE_NEWUTS | syscall.CLONE_NEWPID | syscall.CLONE_NEWNS,
	}
	return cmd.Run()
}

// runChild runs inside the namespaces: it becomes PID 1, sets up the
// filesystem, and execs the user's command.
func runChild(o Options) error {
	if o.PidsMax > 0 {
		// Best effort: cgroup v2 may be read-only or absent.
		if err := limitPids(o.PidsMax); err != nil {
			fmt.Fprintln(os.Stderr, "warning: pids limit not applied:", err)
		}
	}
	if err := syscall.Sethostname([]byte(o.Hostname)); err != nil {
		return fmt.Errorf("sethostname: %w", err)
	}
	// Keep our mounts private so they don't leak back to the host.
	if err := syscall.Mount("", "/", "", syscall.MS_PRIVATE|syscall.MS_REC, ""); err != nil {
		return fmt.Errorf("make mounts private: %w", err)
	}
	if err := syscall.Chroot(o.Root); err != nil {
		return fmt.Errorf("chroot: %w", err)
	}
	if err := os.Chdir("/"); err != nil {
		return err
	}
	if err := os.MkdirAll("/proc", 0o555); err != nil {
		return err
	}
	if err := syscall.Mount("proc", "/proc", "proc", 0, ""); err != nil {
		return fmt.Errorf("mount proc: %w", err)
	}
	defer syscall.Unmount("/proc", 0)

	cmd := exec.Command(o.Command[0], o.Command[1:]...)
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr
	cmd.Env = []string{"PATH=/bin:/usr/bin", "PS1=[container] # "}
	return cmd.Run()
}

func limitPids(n int) error {
	dir := "/sys/fs/cgroup/minicontainer"
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(dir, "pids.max"), []byte(strconv.Itoa(n)), 0o644); err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(dir, "cgroup.procs"), []byte(strconv.Itoa(os.Getpid())), 0o644)
}
