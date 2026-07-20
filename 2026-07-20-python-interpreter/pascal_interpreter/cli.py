"""Command-line entry point: `python3 -m pascal_interpreter <file.pas>`."""

import sys

from .errors import InterpreterError
from .interpreter import Interpreter
from .lexer import Lexer
from .parser import Parser
from .symbols import SemanticAnalyzer


def run(source, output=print):
    lexer = Lexer(source)
    parser = Parser(lexer)
    tree = parser.parse()

    SemanticAnalyzer().analyze(tree)

    interpreter = Interpreter(output=output)
    return interpreter.interpret(tree)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        print("usage: python3 -m pascal_interpreter <file.pas>", file=sys.stderr)
        return 1

    path = argv[0]
    with open(path) as f:
        source = f.read()

    try:
        run(source)
    except InterpreterError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
