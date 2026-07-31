package shell

import (
	"reflect"
	"testing"
)

func TestTokenize(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  []Token
	}{
		{
			name:  "plain words",
			input: "echo hello world",
			want: []Token{
				{Word, "echo"}, {Word, "hello"}, {Word, "world"},
			},
		},
		{
			name:  "extra whitespace collapses",
			input: "  echo   hi  ",
			want:  []Token{{Word, "echo"}, {Word, "hi"}},
		},
		{
			name:  "single quotes are fully literal",
			input: `echo 'a  b "c" $d'`,
			want:  []Token{{Word, "echo"}, {Word, `a  b "c" $d`}},
		},
		{
			name:  "double quotes allow escaped quote and backslash",
			input: `echo "say \"hi\" and \\slash"`,
			want:  []Token{{Word, "echo"}, {Word, `say "hi" and \slash`}},
		},
		{
			name:  "backslash escapes a single char outside quotes",
			input: `echo a\ b`,
			want:  []Token{{Word, "echo"}, {Word, "a b"}},
		},
		{
			name:  "pipe operator",
			input: "ls -la | grep foo",
			want: []Token{
				{Word, "ls"}, {Word, "-la"}, {Pipe, "|"}, {Word, "grep"}, {Word, "foo"},
			},
		},
		{
			name:  "redirects without surrounding whitespace",
			input: "sort<in.txt>out.txt",
			want: []Token{
				{Word, "sort"}, {RedirectIn, "<"}, {Word, "in.txt"},
				{RedirectOut, ">"}, {Word, "out.txt"},
			},
		},
		{
			name:  "append redirect is distinct from single >",
			input: "echo hi >> log.txt",
			want: []Token{
				{Word, "echo"}, {Word, "hi"}, {RedirectAppend, ">>"}, {Word, "log.txt"},
			},
		},
		{
			name:  "comment runs to end of line",
			input: "echo hi # this is ignored | > <",
			want:  []Token{{Word, "echo"}, {Word, "hi"}},
		},
		{
			name:  "hash mid-word is not a comment",
			input: "echo foo#bar",
			want:  []Token{{Word, "echo"}, {Word, "foo#bar"}},
		},
		{
			name:  "empty line",
			input: "",
			want:  nil,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := Tokenize(tt.input)
			if err != nil {
				t.Fatalf("Tokenize(%q) returned error: %v", tt.input, err)
			}
			if !reflect.DeepEqual(got, tt.want) {
				t.Errorf("Tokenize(%q) = %#v, want %#v", tt.input, got, tt.want)
			}
		})
	}
}

func TestTokenizeErrors(t *testing.T) {
	tests := []string{
		`echo 'unterminated`,
		`echo "unterminated`,
		`echo trailing\`,
	}
	for _, in := range tests {
		if _, err := Tokenize(in); err == nil {
			t.Errorf("Tokenize(%q) expected an error, got nil", in)
		}
	}
}
