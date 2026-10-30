// Command calc evaluates expressions from arguments or an interactive prompt.
package main

import (
	"bufio"
	"fmt"
	"os"
	"strings"

	"gocalc/calc"
)

func main() {
	if len(os.Args) > 1 {
		run(strings.Join(os.Args[1:], " "))
		return
	}
	sc := bufio.NewScanner(os.Stdin)
	for fmt.Print("> "); sc.Scan(); fmt.Print("> ") {
		if line := strings.TrimSpace(sc.Text()); line != "" {
			run(line)
		}
	}
}

func run(s string) {
	v, err := calc.Eval(s)
	if err != nil {
		fmt.Println("error:", err)
		return
	}
	fmt.Println(v)
}
