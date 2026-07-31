package shell

import (
	"bytes"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func runLine(t *testing.T, ex *Executor, line string) (int, error) {
	t.Helper()
	toks, err := Tokenize(line)
	if err != nil {
		t.Fatalf("Tokenize(%q): %v", line, err)
	}
	p, err := Parse(toks)
	if err != nil {
		t.Fatalf("Parse(%q): %v", line, err)
	}
	return ex.Run(p)
}

func newTestExecutor(stdin string) (*Executor, *bytes.Buffer, *bytes.Buffer) {
	var out, errOut bytes.Buffer
	ex := &Executor{Stdin: strings.NewReader(stdin), Stdout: &out, Stderr: &errOut}
	return ex, &out, &errOut
}

func TestBuiltinEcho(t *testing.T) {
	ex, out, _ := newTestExecutor("")
	code, err := runLine(t, ex, "echo hello world")
	if err != nil || code != 0 {
		t.Fatalf("code=%d err=%v", code, err)
	}
	if out.String() != "hello world\n" {
		t.Errorf("got %q", out.String())
	}
}

func TestBuiltinEchoDashN(t *testing.T) {
	ex, out, _ := newTestExecutor("")
	if _, err := runLine(t, ex, "echo -n hi"); err != nil {
		t.Fatal(err)
	}
	if out.String() != "hi" {
		t.Errorf("got %q", out.String())
	}
}

func TestBuiltinPwdAndCd(t *testing.T) {
	start, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { os.Chdir(start) })

	tmp := t.TempDir()
	ex, out, _ := newTestExecutor("")
	if _, err := runLine(t, ex, "cd "+tmp); err != nil {
		t.Fatal(err)
	}
	out.Reset()
	if _, err := runLine(t, ex, "pwd"); err != nil {
		t.Fatal(err)
	}
	got := strings.TrimSpace(out.String())
	want, _ := filepath.EvalSymlinks(tmp)
	gotResolved, _ := filepath.EvalSymlinks(got)
	if gotResolved != want {
		t.Errorf("pwd = %q, want %q", got, want)
	}
}

func TestBuiltinCdMissingDir(t *testing.T) {
	ex, _, errOut := newTestExecutor("")
	code, err := runLine(t, ex, "cd /no/such/directory/anywhere")
	if err != nil {
		t.Fatal(err)
	}
	if code == 0 {
		t.Errorf("expected non-zero exit code for missing dir")
	}
	if errOut.Len() == 0 {
		t.Errorf("expected an error message on stderr")
	}
}

func TestExitBuiltin(t *testing.T) {
	ex, _, _ := newTestExecutor("")
	code, err := runLine(t, ex, "exit 7")
	var exitReq *ExitRequest
	if !errors.As(err, &exitReq) {
		t.Fatalf("error was not an *ExitRequest: %v", err)
	}
	if exitReq.Code != 7 || code != 7 {
		t.Errorf("code=%d exitReq.Code=%d, want 7", code, exitReq.Code)
	}
}

func TestExportAffectsSubprocessEnv(t *testing.T) {
	ex, out, _ := newTestExecutor("")
	if _, err := runLine(t, ex, "export GOSH_TEST_VAR=hello123"); err != nil {
		t.Fatal(err)
	}
	defer os.Unsetenv("GOSH_TEST_VAR")

	code, err := runLine(t, ex, "sh -c 'echo $GOSH_TEST_VAR'")
	if err != nil || code != 0 {
		t.Fatalf("code=%d err=%v", code, err)
	}
	if strings.TrimSpace(out.String()) != "hello123" {
		t.Errorf("got %q", out.String())
	}
}

func TestExternalPipeline(t *testing.T) {
	ex, out, _ := newTestExecutor("")
	code, err := runLine(t, ex, "printf 'banana\\napple\\ncherry\\n' | sort | head -n 2")
	if err != nil {
		t.Fatalf("err = %v", err)
	}
	if code != 0 {
		t.Fatalf("code = %d", code)
	}
	if out.String() != "apple\nbanana\n" {
		t.Errorf("got %q", out.String())
	}
}

func TestExternalPipelineExitCodeIsLastStage(t *testing.T) {
	ex, _, _ := newTestExecutor("")
	// grep exits 1 when it finds nothing; that should be the pipeline's code.
	code, err := runLine(t, ex, "echo hello | grep nomatch")
	if err != nil {
		t.Fatalf("err = %v", err)
	}
	if code != 1 {
		t.Errorf("code = %d, want 1", code)
	}
}

func TestCommandNotFound(t *testing.T) {
	ex, _, errOut := newTestExecutor("")
	code, err := runLine(t, ex, "this-command-does-not-exist-anywhere")
	if err != nil {
		t.Fatalf("err = %v", err)
	}
	if code != 127 {
		t.Errorf("code = %d, want 127", code)
	}
	if !strings.Contains(errOut.String(), "command not found") {
		t.Errorf("stderr = %q", errOut.String())
	}
}

func TestRedirectOutAndIn(t *testing.T) {
	dir := t.TempDir()
	outFile := filepath.Join(dir, "out.txt")

	ex, _, _ := newTestExecutor("")
	if _, err := runLine(t, ex, "echo redirected > "+outFile); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(outFile)
	if err != nil {
		t.Fatal(err)
	}
	if string(data) != "redirected\n" {
		t.Errorf("file contents = %q", data)
	}

	ex2, out2, _ := newTestExecutor("")
	if _, err := runLine(t, ex2, "cat < "+outFile); err != nil {
		t.Fatal(err)
	}
	if out2.String() != "redirected\n" {
		t.Errorf("got %q", out2.String())
	}
}

func TestRedirectAppend(t *testing.T) {
	dir := t.TempDir()
	outFile := filepath.Join(dir, "log.txt")

	ex, _, _ := newTestExecutor("")
	if _, err := runLine(t, ex, "echo one > "+outFile); err != nil {
		t.Fatal(err)
	}
	if _, err := runLine(t, ex, "echo two >> "+outFile); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(outFile)
	if err != nil {
		t.Fatal(err)
	}
	if string(data) != "one\ntwo\n" {
		t.Errorf("file contents = %q", data)
	}
}

func TestBuiltinRedirectOut(t *testing.T) {
	dir := t.TempDir()
	outFile := filepath.Join(dir, "builtin-out.txt")

	ex, out, _ := newTestExecutor("")
	if _, err := runLine(t, ex, "echo from-builtin > "+outFile); err != nil {
		t.Fatal(err)
	}
	if out.Len() != 0 {
		t.Errorf("stdout buffer should be untouched, got %q", out.String())
	}
	data, err := os.ReadFile(outFile)
	if err != nil {
		t.Fatal(err)
	}
	if string(data) != "from-builtin\n" {
		t.Errorf("file contents = %q", data)
	}
}
