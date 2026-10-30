package calc

import (
	"math"
	"strings"
	"testing"
)

func TestEval(t *testing.T) {
	cases := []struct {
		in   string
		want float64
	}{
		{"1+2", 3},
		{"2+3*4", 14},
		{"(2+3)*4", 20},
		{"10-4-3", 3},
		{"100/10/2", 5},
		{"7%4", 3},
		{"-3+5", 2},
		{"--3", 3},
		{"2^3^2", 512},
		{"-2^2", -4},
		{"2^-1", 0.5},
		{" 1.5 * 4 ", 6},
		{"sqrt(16)+abs(-2)", 6},
		{"floor(2.7)+ceil(2.1)", 5},
		{"2*pi", 2 * math.Pi},
		{"ln(e)", 1},
	}
	for _, c := range cases {
		got, err := Eval(c.in)
		if err != nil {
			t.Errorf("%q: %v", c.in, err)
			continue
		}
		if math.Abs(got-c.want) > 1e-9 {
			t.Errorf("%q = %v, want %v", c.in, got, c.want)
		}
	}
}

func TestErrors(t *testing.T) {
	cases := map[string]string{
		"":       "end of input",
		"1+":     "end of input",
		"(1+2":   "missing ')'",
		"1/0":    "division by zero",
		"5%0":    "modulo by zero",
		"foo(1)": "unknown function",
		"bar":    "unknown identifier",
		"1 2":    "unexpected",
		"1..2":   "bad number",
		"3 $ 4":  "unexpected",
	}
	for in, want := range cases {
		_, err := Eval(in)
		if err == nil || !strings.Contains(err.Error(), want) {
			t.Errorf("%q: err = %v, want containing %q", in, err, want)
		}
	}
}
