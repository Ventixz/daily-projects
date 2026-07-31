package shell

import (
	"reflect"
	"testing"
)

func mustTokenize(t *testing.T, line string) []Token {
	t.Helper()
	toks, err := Tokenize(line)
	if err != nil {
		t.Fatalf("Tokenize(%q): %v", line, err)
	}
	return toks
}

func TestParseSingleCommand(t *testing.T) {
	p, err := Parse(mustTokenize(t, "echo hello world"))
	if err != nil {
		t.Fatalf("Parse: %v", err)
	}
	want := &Pipeline{Commands: []*Command{
		{Args: []string{"echo", "hello", "world"}},
	}}
	if !reflect.DeepEqual(p, want) {
		t.Errorf("got %+v, want %+v", p, want)
	}
}

func TestParsePipeline(t *testing.T) {
	p, err := Parse(mustTokenize(t, "cat file.txt | grep foo | wc -l"))
	if err != nil {
		t.Fatalf("Parse: %v", err)
	}
	if len(p.Commands) != 3 {
		t.Fatalf("got %d commands, want 3", len(p.Commands))
	}
	if !reflect.DeepEqual(p.Commands[0].Args, []string{"cat", "file.txt"}) {
		t.Errorf("stage 0 args = %v", p.Commands[0].Args)
	}
	if !reflect.DeepEqual(p.Commands[1].Args, []string{"grep", "foo"}) {
		t.Errorf("stage 1 args = %v", p.Commands[1].Args)
	}
	if !reflect.DeepEqual(p.Commands[2].Args, []string{"wc", "-l"}) {
		t.Errorf("stage 2 args = %v", p.Commands[2].Args)
	}
}

func TestParseRedirects(t *testing.T) {
	p, err := Parse(mustTokenize(t, "sort < in.txt > out.txt"))
	if err != nil {
		t.Fatalf("Parse: %v", err)
	}
	cmd := p.Commands[0]
	if !reflect.DeepEqual(cmd.Args, []string{"sort"}) {
		t.Errorf("args = %v", cmd.Args)
	}
	want := []Redirect{{RedirIn, "in.txt"}, {RedirOut, "out.txt"}}
	if !reflect.DeepEqual(cmd.Redirects, want) {
		t.Errorf("redirects = %+v, want %+v", cmd.Redirects, want)
	}
}

func TestParseAppendRedirect(t *testing.T) {
	p, err := Parse(mustTokenize(t, "echo hi >> log.txt"))
	if err != nil {
		t.Fatalf("Parse: %v", err)
	}
	want := []Redirect{{RedirAppend, "log.txt"}}
	if !reflect.DeepEqual(p.Commands[0].Redirects, want) {
		t.Errorf("redirects = %+v, want %+v", p.Commands[0].Redirects, want)
	}
}

func TestParseEmptyInput(t *testing.T) {
	p, err := Parse(nil)
	if err != nil {
		t.Fatalf("Parse(nil): %v", err)
	}
	if len(p.Commands) != 0 {
		t.Errorf("expected no commands, got %+v", p.Commands)
	}
}

func TestParseErrors(t *testing.T) {
	tests := []string{
		"| grep foo",     // leading pipe
		"grep foo |",     // trailing pipe
		"grep foo || wc", // empty stage between pipes
		"echo hi >",      // redirect with no target
		"echo hi > | wc", // redirect target is itself an operator
	}
	for _, in := range tests {
		toks := mustTokenize(t, in)
		if _, err := Parse(toks); err == nil {
			t.Errorf("Parse(%q) expected an error, got nil", in)
		}
	}
}
