import sys

from . import LexerError, ParserError, SemanticError, InterpreterError, run_source


def main():
    if len(sys.argv) != 2:
        print("usage: python -m pascal_interpreter <path/to/program.pas>", file=sys.stderr)
        sys.exit(2)

    path = sys.argv[1]
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()

    try:
        scope = run_source(text)
    except (LexerError, ParserError, SemanticError, InterpreterError) as exc:
        print(f"{type(exc).__name__}: {exc}", file=sys.stderr)
        sys.exit(1)

    print("Final global scope:")
    for name, value in sorted(scope.items()):
        print(f"  {name} = {value!r}")


if __name__ == "__main__":
    main()
