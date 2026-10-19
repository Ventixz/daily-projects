package main

import (
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"syscall"
)

const usage = `usage:
  gocontainer mkrootfs DIR BIN...
  gocontainer run --rootfs DIR [--hostname H] [--mem BYTES] [--pids N] -- CMD [ARGS]`

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, usage)
		os.Exit(2)
	}
	var err error
	switch os.Args[1] {
	case "mkrootfs":
		if len(os.Args) < 4 {
			fmt.Fprintln(os.Stderr, usage)
			os.Exit(2)
		}
		err = MakeRootfs(os.Args[2], os.Args[3:])
	case "run":
		err = run(os.Args[2:])
	case "child":
		err = child(os.Args[2:])
	default:
		fmt.Fprintln(os.Stderr, usage)
		os.Exit(2)
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

// run re-executes this binary as "child" inside new namespaces. Re-exec is
// needed because a PID namespace only applies to *children* of the creator.
func run(args []string) error {
	c, err := ParseArgs(args)
	if err != nil {
		return err
	}
	cmd := exec.Command("/proc/self/exe", append([]string{"child"}, args...)...)
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr
	cmd.SysProcAttr = &syscall.SysProcAttr{
		Cloneflags: syscall.CLONE_NEWUTS | syscall.CLONE_NEWPID | syscall.CLONE_NEWNS,
	}
	if os.Geteuid() != 0 {
		// Rootless: map our uid to root inside a new user namespace.
		cmd.SysProcAttr.Cloneflags |= syscall.CLONE_NEWUSER
		cmd.SysProcAttr.UidMappings = []syscall.SysProcIDMap{{ContainerID: 0, HostID: os.Getuid(), Size: 1}}
		cmd.SysProcAttr.GidMappings = []syscall.SysProcIDMap{{ContainerID: 0, HostID: os.Getgid(), Size: 1}}
	}
	if err := cmd.Start(); err != nil {
		return err
	}
	if c.MemBytes > 0 || c.Pids > 0 {
		dir := "/sys/fs/cgroup/gocontainer-" + strconv.Itoa(cmd.Process.Pid)
		if err := ApplyCgroup(dir, c, cmd.Process.Pid); err != nil {
			fmt.Fprintln(os.Stderr, "warning: cgroup limits not applied:", err)
		} else {
			defer os.Remove(dir)
		}
	}
	return cmd.Wait()
}

// child runs as PID 1 of the new PID namespace.
func child(args []string) error {
	c, err := ParseArgs(args)
	if err != nil {
		return err
	}
	if err := syscall.Sethostname([]byte(c.Hostname)); err != nil {
		return fmt.Errorf("sethostname: %w", err)
	}
	// Keep our mounts from propagating back to the host.
	if err := syscall.Mount("", "/", "", syscall.MS_REC|syscall.MS_PRIVATE, ""); err != nil {
		return fmt.Errorf("make mounts private: %w", err)
	}
	if err := syscall.Chroot(c.Rootfs); err != nil {
		return fmt.Errorf("chroot: %w", err)
	}
	if err := os.Chdir("/"); err != nil {
		return err
	}
	if err := syscall.Mount("proc", "/proc", "proc", 0, ""); err != nil {
		return fmt.Errorf("mount proc: %w", err)
	}
	// Exec (don't fork) so the user's command becomes PID 1 of the container.
	// The proc mount disappears with the mount namespace when it exits.
	path, err := exec.LookPath(c.Cmd[0])
	if err != nil {
		return err
	}
	return syscall.Exec(path, c.Cmd, []string{"PATH=/bin", "HOME=/"})
}
