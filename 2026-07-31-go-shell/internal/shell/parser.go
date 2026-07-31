package shell

import "fmt"

type RedirectKind int

const (
	RedirIn RedirectKind = iota
	RedirOut
	RedirAppend
)

type Redirect struct {
	Kind   RedirectKind
	Target string
}

// Command is one stage of a pipeline: a program and its arguments, plus any
// redirects that apply to that stage specifically (e.g. `grep x < in.txt`).
type Command struct {
	Args      []string
	Redirects []Redirect
}

// Pipeline is one or more commands connected by `|`.
type Pipeline struct {
	Commands []*Command
}

// Parse turns a token stream into a Pipeline. It rejects the shapes that
// can't mean anything: a leading/trailing/doubled `|`, and a redirect
// operator with no filename after it.
func Parse(tokens []Token) (*Pipeline, error) {
	if len(tokens) == 0 {
		return &Pipeline{}, nil
	}

	var pipeline Pipeline
	cmd := &Command{}
	commandHasContent := false

	appendCommand := func() error {
		if !commandHasContent {
			return fmt.Errorf("syntax error: empty command")
		}
		pipeline.Commands = append(pipeline.Commands, cmd)
		cmd = &Command{}
		commandHasContent = false
		return nil
	}

	for i := 0; i < len(tokens); i++ {
		tok := tokens[i]
		switch tok.Kind {
		case Word:
			cmd.Args = append(cmd.Args, tok.Value)
			commandHasContent = true

		case Pipe:
			if err := appendCommand(); err != nil {
				return nil, err
			}

		case RedirectIn, RedirectOut, RedirectAppend:
			if i+1 >= len(tokens) || tokens[i+1].Kind != Word {
				return nil, fmt.Errorf("syntax error: expected filename after %q", tok.Value)
			}
			kind := map[TokenKind]RedirectKind{
				RedirectIn:     RedirIn,
				RedirectOut:    RedirOut,
				RedirectAppend: RedirAppend,
			}[tok.Kind]
			cmd.Redirects = append(cmd.Redirects, Redirect{Kind: kind, Target: tokens[i+1].Value})
			commandHasContent = true
			i++
		}
	}
	if err := appendCommand(); err != nil {
		return nil, err
	}
	return &pipeline, nil
}
