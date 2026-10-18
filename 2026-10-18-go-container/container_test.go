package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func TestParseArgs(t *testing.T) {
	o, err := ParseArgs([]string{"--root", "/r", "--hostname", "h", "--pids", "20", "--", "sh", "-c", "ls"})
	if err != nil {
		t.Fatal(err)
	}
	if o.Root != "/r" || o.Hostname != "h" || o.PidsMax != 20 || strings.Join(o.Command, " ") != "sh -c ls" {
		t.Fatalf("unexpected options: %+v", o)
	}
}

func TestParseArgsDefaultsAndErrors(t *testing.T) {
	o, err := ParseArgs([]string{"--root", "/r", "sh"})
	if err != nil || o.Hostname != "container" || o.Command[0] != "sh" {
		t.Fatalf("defaults wrong: %+v %v", o, err)
	}
	bad := [][]string{
		{"--", "sh"},                          // no root
		{"--root", "/r"},                      // no command
		{"--root"},                            // missing value
		{"--root", "/r", "--pids", "x", "sh"}, // bad number
		{"--root", "/r", "--bogus", "1", "sh"},
	}
	for _, args := range bad {
		if _, err := ParseArgs(args); err == nil {
			t.Errorf("expected error for %v", args)
		}
	}
}

// Integration test: needs root and namespace support, otherwise it skips.
func TestContainerIsolation(t *testing.T) {
	if os.Geteuid() != 0 {
		t.Skip("needs root")
	}
	if err := exec.Command("unshare", "--pid", "--uts", "--mount", "--fork", "true").Run(); err != nil {
		t.Skip("namespaces unavailable:", err)
	}
	tmp := t.TempDir()
	root := filepath.Join(tmp, "rootfs")
	if out, err := exec.Command("./make-rootfs.sh", root).CombinedOutput(); err != nil {
		t.Fatalf("rootfs: %v\n%s", err, out)
	}
	bin := filepath.Join(tmp, "mc")
	if out, err := exec.Command("go", "build", "-o", bin, ".").CombinedOutput(); err != nil {
		t.Fatalf("build: %v\n%s", err, out)
	}
	run := func(script string) string {
		out, err := exec.Command(bin, "run", "--root", root, "--hostname", "boxed", "--", "/bin/sh", "-c", script).CombinedOutput()
		if err != nil {
			t.Fatalf("run %q: %v\n%s", script, err, out)
		}
		return strings.TrimSpace(string(out))
	}
	if got := run("hostname"); got != "boxed" {
		t.Errorf("hostname = %q, want boxed", got)
	}
	if host, _ := os.Hostname(); host == "boxed" {
		t.Error("hostname leaked to host")
	}
	// The Go child is PID 1; the shell is one of only a handful of processes,
	// not the hundreds visible on the host.
	if got := run("ps"); !strings.Contains(got, "    1 ") || len(strings.Split(got, "\n")) > 6 {
		t.Errorf("PID namespace not isolated:\n%s", got)
	}
	if got := run("ls /"); strings.Contains(got, "home") || !strings.Contains(got, "bin") {
		t.Errorf("root listing looks wrong: %q", got)
	}
	// exit codes propagate
	err := exec.Command(bin, "run", "--root", root, "--", "/bin/sh", "-c", "exit 7").Run()
	if ee, ok := err.(*exec.ExitError); !ok || ee.ExitCode() != 7 {
		t.Errorf("exit code not propagated: %v", err)
	}
}
