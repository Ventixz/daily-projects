// Package calc evaluates arithmetic expressions with a recursive-descent parser.
//
// Grammar (lowest to highest precedence):
//
//	expr   = term { ("+" | "-") term }
//	term   = unary { ("*" | "/" | "%") unary }
//	unary  = "-" unary | power
//	power  = atom [ "^" unary ]        (right associative)
//	atom   = number | ident "(" expr ")" | ident | "(" expr ")"
package calc

import (
	"errors"
	"fmt"
	"math"
	"strconv"
	"unicode"
)

var consts = map[string]float64{"pi": math.Pi, "e": math.E}

var funcs = map[string]func(float64) float64{
	"sqrt": math.Sqrt, "abs": math.Abs, "sin": math.Sin, "cos": math.Cos,
	"tan": math.Tan, "ln": math.Log, "floor": math.Floor, "ceil": math.Ceil,
}

type parser struct {
	src []rune
	pos int
}

// Eval parses and evaluates expr.
func Eval(expr string) (float64, error) {
	p := &parser{src: []rune(expr)}
	v, err := p.expr()
	if err != nil {
		return 0, err
	}
	p.skip()
	if p.pos < len(p.src) {
		return 0, fmt.Errorf("unexpected %q at %d", p.src[p.pos], p.pos)
	}
	return v, nil
}

func (p *parser) skip() {
	for p.pos < len(p.src) && unicode.IsSpace(p.src[p.pos]) {
		p.pos++
	}
}

func (p *parser) peek() rune {
	p.skip()
	if p.pos >= len(p.src) {
		return 0
	}
	return p.src[p.pos]
}

func (p *parser) expr() (float64, error) {
	l, err := p.term()
	if err != nil {
		return 0, err
	}
	for {
		op := p.peek()
		if op != '+' && op != '-' {
			return l, nil
		}
		p.pos++
		r, err := p.term()
		if err != nil {
			return 0, err
		}
		if op == '+' {
			l += r
		} else {
			l -= r
		}
	}
}

func (p *parser) term() (float64, error) {
	l, err := p.unary()
	if err != nil {
		return 0, err
	}
	for {
		op := p.peek()
		if op != '*' && op != '/' && op != '%' {
			return l, nil
		}
		p.pos++
		r, err := p.unary()
		if err != nil {
			return 0, err
		}
		switch op {
		case '*':
			l *= r
		case '/':
			if r == 0 {
				return 0, errors.New("division by zero")
			}
			l /= r
		case '%':
			if r == 0 {
				return 0, errors.New("modulo by zero")
			}
			l = math.Mod(l, r)
		}
	}
}

func (p *parser) unary() (float64, error) {
	if p.peek() == '-' {
		p.pos++
		v, err := p.unary()
		return -v, err
	}
	return p.power()
}

func (p *parser) power() (float64, error) {
	b, err := p.atom()
	if err != nil {
		return 0, err
	}
	if p.peek() == '^' {
		p.pos++
		x, err := p.unary()
		if err != nil {
			return 0, err
		}
		return math.Pow(b, x), nil
	}
	return b, nil
}

func (p *parser) atom() (float64, error) {
	c := p.peek()
	switch {
	case c == 0:
		return 0, errors.New("unexpected end of input")
	case c == '(':
		p.pos++
		v, err := p.expr()
		if err != nil {
			return 0, err
		}
		if p.peek() != ')' {
			return 0, fmt.Errorf("missing ')' at %d", p.pos)
		}
		p.pos++
		return v, nil
	case unicode.IsDigit(c) || c == '.':
		start := p.pos
		for p.pos < len(p.src) && (unicode.IsDigit(p.src[p.pos]) || p.src[p.pos] == '.') {
			p.pos++
		}
		v, err := strconv.ParseFloat(string(p.src[start:p.pos]), 64)
		if err != nil {
			return 0, fmt.Errorf("bad number %q", string(p.src[start:p.pos]))
		}
		return v, nil
	case unicode.IsLetter(c):
		start := p.pos
		for p.pos < len(p.src) && unicode.IsLetter(p.src[p.pos]) {
			p.pos++
		}
		name := string(p.src[start:p.pos])
		if p.peek() == '(' {
			f, ok := funcs[name]
			if !ok {
				return 0, fmt.Errorf("unknown function %q", name)
			}
			p.pos++
			v, err := p.expr()
			if err != nil {
				return 0, err
			}
			if p.peek() != ')' {
				return 0, fmt.Errorf("missing ')' at %d", p.pos)
			}
			p.pos++
			return f(v), nil
		}
		if v, ok := consts[name]; ok {
			return v, nil
		}
		return 0, fmt.Errorf("unknown identifier %q", name)
	}
	return 0, fmt.Errorf("unexpected %q at %d", c, p.pos)
}
