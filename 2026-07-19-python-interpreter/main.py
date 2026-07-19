#!/usr/bin/env python3
"""Entry point: run a .calc script if given a path, otherwise start a REPL."""
import sys

from interpreter.errors import InterpreterError
from interpreter.interpreter import Interpreter
from interpreter.lexer import Lexer
from interpreter.parser import Parser


def run_line(line, interpreter):
    tree = Parser(Lexer(line)).parse()
    return interpreter.interpret(tree)


def run_script(path):
    interpreter = Interpreter()
    with open(path) as f:
        for lineno, raw_line in enumerate(f, start=1):
            line = raw_line.split("#", 1)[0].strip()
            if not line:
                continue
            try:
                result = run_line(line, interpreter)
            except InterpreterError as exc:
                print(f"{path}:{lineno}: error: {exc}", file=sys.stderr)
                sys.exit(1)
            print(result)


def run_repl():
    interpreter = Interpreter()
    print("calc REPL — arithmetic + variables. Ctrl-D or 'exit' to quit.")
    while True:
        try:
            line = input("calc> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break
        if line in ("", "exit", "quit"):
            if line == "":
                continue
            break
        try:
            print(run_line(line, interpreter))
        except InterpreterError as exc:
            print(f"error: {exc}")


def main():
    if len(sys.argv) > 1:
        run_script(sys.argv[1])
    else:
        run_repl()


if __name__ == "__main__":
    main()
