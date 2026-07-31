// Command gosh is a small interactive Unix shell: it reads a line, tokenizes
// it, parses it into a pipeline, and runs that pipeline against real
// subprocesses. See internal/shell for the actual implementation.
package main

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"os"

	"gosh/internal/shell"
)

func main() {
	os.Exit(run(os.Stdin, os.Stdout, os.Stderr, true))
}

func run(stdin io.Reader, stdout, stderr io.Writer, interactive bool) int {
	ex := &shell.Executor{Stdin: stdin, Stdout: stdout, Stderr: stderr}
	scanner := bufio.NewScanner(stdin)

	lastCode := 0
	for {
		if interactive {
			wd, _ := os.Getwd()
			fmt.Fprintf(stdout, "gosh:%s$ ", wd)
		}
		if !scanner.Scan() {
			break
		}
		line := scanner.Text()

		tokens, err := shell.Tokenize(line)
		if err != nil {
			fmt.Fprintf(stderr, "gosh: %s\n", err)
			lastCode = 2
			continue
		}
		pipeline, err := shell.Parse(tokens)
		if err != nil {
			fmt.Fprintf(stderr, "gosh: %s\n", err)
			lastCode = 2
			continue
		}
		if len(pipeline.Commands) == 0 {
			continue
		}

		code, err := ex.Run(pipeline)
		var exitReq *shell.ExitRequest
		if errors.As(err, &exitReq) {
			return exitReq.Code
		}
		lastCode = code
	}
	return lastCode
}
