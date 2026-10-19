package main

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
)

// Config is everything the parent process tells the child about the container.
type Config struct {
	Rootfs   string
	Hostname string
	MemBytes int64 // 0 = unlimited
	Pids     int   // 0 = unlimited
	Cmd      []string
}

// ParseArgs parses: [--rootfs DIR] [--hostname NAME] [--mem BYTES] [--pids N] -- cmd args...
func ParseArgs(args []string) (Config, error) {
	c := Config{Hostname: "container"}
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
			return c, fmt.Errorf("flag %s needs a value", a)
		}
		v := args[i+1]
		i++
		switch a {
		case "--rootfs":
			c.Rootfs = v
		case "--hostname":
			c.Hostname = v
		case "--mem":
			n, err := strconv.ParseInt(v, 10, 64)
			if err != nil || n < 0 {
				return c, fmt.Errorf("bad --mem %q", v)
			}
			c.MemBytes = n
		case "--pids":
			n, err := strconv.Atoi(v)
			if err != nil || n < 0 {
				return c, fmt.Errorf("bad --pids %q", v)
			}
			c.Pids = n
		default:
			return c, fmt.Errorf("unknown flag %s", a)
		}
	}
	c.Cmd = args[i:]
	if c.Rootfs == "" {
		return c, errors.New("--rootfs is required")
	}
	if len(c.Cmd) == 0 {
		return c, errors.New("no command given")
	}
	return c, nil
}

// CgroupWrites returns the file->content pairs (relative to a cgroup v2
// directory) that enforce the limits, plus the pid to move into it.
func CgroupWrites(c Config, pid int) map[string]string {
	w := map[string]string{}
	if c.MemBytes > 0 {
		w["memory.max"] = strconv.FormatInt(c.MemBytes, 10)
	}
	if c.Pids > 0 {
		w["pids.max"] = strconv.Itoa(c.Pids)
	}
	if len(w) > 0 {
		w["cgroup.procs"] = strconv.Itoa(pid)
	}
	return w
}

// ApplyCgroup writes the limits under dir (created if missing).
func ApplyCgroup(dir string, c Config, pid int) error {
	w := CgroupWrites(c, pid)
	if len(w) == 0 {
		return nil
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	// cgroup.procs last so limits are in place before the process joins.
	for _, name := range []string{"memory.max", "pids.max", "cgroup.procs"} {
		if v, ok := w[name]; ok {
			if err := os.WriteFile(filepath.Join(dir, name), []byte(v), 0o644); err != nil {
				return fmt.Errorf("%s: %w", name, err)
			}
		}
	}
	return nil
}

// LibsFromLdd extracts absolute library paths from `ldd` output.
func LibsFromLdd(out string) []string {
	var libs []string
	for _, line := range strings.Split(out, "\n") {
		line = strings.TrimSpace(line)
		if i := strings.Index(line, "=>"); i >= 0 {
			line = strings.TrimSpace(line[i+2:])
		}
		f := strings.Fields(line)
		if len(f) > 0 && strings.HasPrefix(f[0], "/") {
			libs = append(libs, f[0])
		}
	}
	return libs
}

// MakeRootfs creates a minimal root filesystem at dir holding the given
// binaries and the shared libraries they need.
func MakeRootfs(dir string, bins []string) error {
	for _, d := range []string{"bin", "proc", "tmp", "lib", "lib64", "usr/lib"} {
		if err := os.MkdirAll(filepath.Join(dir, d), 0o755); err != nil {
			return err
		}
	}
	for _, b := range bins {
		path, err := exec.LookPath(b)
		if err != nil {
			return err
		}
		if err := copyInto(dir, path, filepath.Join("/bin", filepath.Base(path))); err != nil {
			return err
		}
		out, err := exec.Command("ldd", path).CombinedOutput()
		if err != nil {
			continue // statically linked
		}
		for _, lib := range LibsFromLdd(string(out)) {
			if err := copyInto(dir, lib, lib); err != nil {
				return err
			}
		}
	}
	return nil
}

func copyInto(root, src, dst string) error {
	data, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	full := filepath.Join(root, dst)
	if err := os.MkdirAll(filepath.Dir(full), 0o755); err != nil {
		return err
	}
	return os.WriteFile(full, data, 0o755)
}
