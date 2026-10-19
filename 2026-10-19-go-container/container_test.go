package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

func TestParseArgs(t *testing.T) {
	c, err := ParseArgs([]string{"--rootfs", "/r", "--hostname", "h", "--mem", "1024", "--pids", "5", "--", "sh", "-c", "x"})
	if err != nil {
		t.Fatal(err)
	}
	want := Config{Rootfs: "/r", Hostname: "h", MemBytes: 1024, Pids: 5, Cmd: []string{"sh", "-c", "x"}}
	if !reflect.DeepEqual(c, want) {
		t.Fatalf("got %+v", c)
	}
}

func TestParseArgsDefaultsAndErrors(t *testing.T) {
	c, err := ParseArgs([]string{"--rootfs", "/r", "sh"})
	if err != nil || c.Hostname != "container" || c.Cmd[0] != "sh" {
		t.Fatalf("%+v %v", c, err)
	}
	for _, bad := range [][]string{
		{"--", "sh"},       // no rootfs
		{"--rootfs", "/r"}, // no command
		{"--rootfs", "/r", "--mem", "x", "--", "sh"},
		{"--rootfs", "/r", "--pids", "-1", "--", "sh"},
		{"--rootfs", "/r", "--bogus", "1", "--", "sh"},
		{"--rootfs"},
	} {
		if _, err := ParseArgs(bad); err == nil {
			t.Errorf("expected error for %v", bad)
		}
	}
}

func TestCgroupWrites(t *testing.T) {
	if got := CgroupWrites(Config{}, 7); len(got) != 0 {
		t.Fatalf("no limits should write nothing, got %v", got)
	}
	got := CgroupWrites(Config{MemBytes: 100, Pids: 3}, 42)
	want := map[string]string{"memory.max": "100", "pids.max": "3", "cgroup.procs": "42"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v", got)
	}
}

func TestApplyCgroup(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "cg")
	if err := ApplyCgroup(dir, Config{Pids: 9}, 11); err != nil {
		t.Fatal(err)
	}
	b, _ := os.ReadFile(filepath.Join(dir, "pids.max"))
	p, _ := os.ReadFile(filepath.Join(dir, "cgroup.procs"))
	if string(b) != "9" || string(p) != "11" {
		t.Fatalf("pids.max=%q procs=%q", b, p)
	}
	if _, err := os.Stat(filepath.Join(dir, "memory.max")); err == nil {
		t.Fatal("memory.max should not be written when unset")
	}
}

func TestLibsFromLdd(t *testing.T) {
	out := "\tlinux-vdso.so.1 (0x00007f)\n\tlibc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x1)\n\t/lib64/ld-linux-x86-64.so.2 (0x2)\n\tlibgone.so => not found\n"
	got := LibsFromLdd(out)
	want := []string{"/lib/x86_64-linux-gnu/libc.so.6", "/lib64/ld-linux-x86-64.so.2"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v", got)
	}
}

func TestMakeRootfs(t *testing.T) {
	dir := t.TempDir()
	if err := MakeRootfs(dir, []string{"sh"}); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(dir, "bin", "sh")); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(dir, "proc")); err != nil {
		t.Fatal(err)
	}
	if err := MakeRootfs(t.TempDir(), []string{"no-such-binary-xyz"}); err == nil {
		t.Fatal("expected error for missing binary")
	}
}

// End-to-end: needs working user namespaces, so it skips where unavailable.
func TestRunIsolated(t *testing.T) {
	if exec.Command("unshare", "--user", "--map-root-user", "--pid", "--fork", "--uts", "--mount", "true").Run() != nil {
		t.Skip("namespaces unavailable")
	}
	tmp := t.TempDir()
	bin := filepath.Join(tmp, "gocontainer")
	if out, err := exec.Command("go", "build", "-o", bin, ".").CombinedOutput(); err != nil {
		t.Fatalf("build: %v\n%s", err, out)
	}
	root := filepath.Join(tmp, "rootfs")
	if out, err := exec.Command(bin, "mkrootfs", root, "sh", "hostname", "ls", "cat").CombinedOutput(); err != nil {
		t.Fatalf("mkrootfs: %v\n%s", err, out)
	}
	out, err := exec.Command(bin, "run", "--rootfs", root, "--hostname", "boxed", "--",
		"/bin/sh", "-c", "hostname; echo pid=$$; ls /proc").CombinedOutput()
	if err != nil {
		t.Fatalf("run: %v\n%s", err, out)
	}
	lines := strings.Fields(string(out))
	if len(lines) < 2 || lines[0] != "boxed" || lines[1] != "pid=1" {
		t.Fatalf("unexpected output %q", out)
	}
	// Only the container's own processes (sh and ls) are visible in /proc.
	n := 0
	for _, e := range lines[2:] {
		if e[0] >= '0' && e[0] <= '9' {
			n++
		}
	}
	if n != 2 {
		t.Fatalf("expected 2 visible pids, got %d: %q", n, out)
	}
	if h, _ := os.Hostname(); h == "boxed" {
		t.Fatal("hostname leaked to host")
	}
}
