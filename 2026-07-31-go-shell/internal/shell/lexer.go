// Package shell implements a small POSIX-ish shell: a lexer that turns a
// line of input into tokens, a parser that turns tokens into a pipeline of
// commands with redirects, and an executor that runs that pipeline.
package shell

import (
	"fmt"
	"strings"
)

// TokenKind classifies a single lexer token.
type TokenKind int

const (
	Word TokenKind = iota
	Pipe
	RedirectIn
	RedirectOut
	RedirectAppend
)

type Token struct {
	Kind  TokenKind
	Value string
}

// Tokenize splits a line into words and operators, honoring single quotes
// (fully literal), double quotes (literal except \", \\ and \$), and
// backslash-escaping a single character outside quotes. Operators (| < > >>)
// split a word even without surrounding whitespace, e.g. `out>file` lexes as
// three tokens, matching how a real shell reads it. A `#` that starts a word
// begins a comment running to end of line.
func Tokenize(line string) ([]Token, error) {
	var tokens []Token
	var cur strings.Builder
	haveWord := false

	flush := func() {
		if haveWord {
			tokens = append(tokens, Token{Kind: Word, Value: cur.String()})
			cur.Reset()
			haveWord = false
		}
	}

	runes := []rune(line)
	i := 0
	atWordStart := true
	for i < len(runes) {
		c := runes[i]

		switch {
		case c == '\'':
			haveWord = true
			j := i + 1
			for j < len(runes) && runes[j] != '\'' {
				cur.WriteRune(runes[j])
				j++
			}
			if j >= len(runes) {
				return nil, fmt.Errorf("unterminated single quote")
			}
			i = j + 1
			atWordStart = false
			continue

		case c == '"':
			haveWord = true
			j := i + 1
			for j < len(runes) && runes[j] != '"' {
				if runes[j] == '\\' && j+1 < len(runes) && (runes[j+1] == '"' || runes[j+1] == '\\' || runes[j+1] == '$') {
					cur.WriteRune(runes[j+1])
					j += 2
					continue
				}
				cur.WriteRune(runes[j])
				j++
			}
			if j >= len(runes) {
				return nil, fmt.Errorf("unterminated double quote")
			}
			i = j + 1
			atWordStart = false
			continue

		case c == '\\':
			if i+1 >= len(runes) {
				return nil, fmt.Errorf("trailing backslash")
			}
			haveWord = true
			cur.WriteRune(runes[i+1])
			i += 2
			atWordStart = false
			continue

		case c == '#' && atWordStart:
			flush()
			return tokens, nil

		case c == ' ' || c == '\t':
			flush()
			i++
			atWordStart = true
			continue

		case c == '|':
			flush()
			tokens = append(tokens, Token{Kind: Pipe, Value: "|"})
			i++
			atWordStart = true
			continue

		case c == '>':
			flush()
			if i+1 < len(runes) && runes[i+1] == '>' {
				tokens = append(tokens, Token{Kind: RedirectAppend, Value: ">>"})
				i += 2
			} else {
				tokens = append(tokens, Token{Kind: RedirectOut, Value: ">"})
				i++
			}
			atWordStart = true
			continue

		case c == '<':
			flush()
			tokens = append(tokens, Token{Kind: RedirectIn, Value: "<"})
			i++
			atWordStart = true
			continue

		default:
			haveWord = true
			cur.WriteRune(c)
			i++
			atWordStart = false
			continue
		}
	}
	flush()
	return tokens, nil
}
