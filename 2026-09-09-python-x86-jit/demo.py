#!/usr/bin/env python3
"""Compile an arithmetic expression to x86-64 machine code, run it, and
show the interpreter's answer and the raw bytes alongside it.

    $ python3 demo.py "3 + 4 * (2 - 5)"
    $ python3 demo.py "a * a + b" a=6 b=1
    $ python3 demo.py   # interactive REPL, no variables
"""

import sys

from src.parser import parse
from src.codegen import compile_to_machine_code
from src.interpreter import evaluate
from src.jit import JitFunction


def run_one(expr_text, variables):
    ast = parse(expr_text)
    machine_code = compile_to_machine_code(ast)
    jit_result = JitFunction(machine_code).call(variables)
    interp_result = evaluate(ast, variables)
    hexdump = " ".join(f"{b:02x}" for b in machine_code)
    print(f"expr:      {expr_text}")
    if variables:
        print(f"variables: {variables}")
    print(f"jit:       {jit_result}")
    print(f"interp:    {interp_result}")
    print(f"match:     {jit_result == interp_result}")
    print(f"machine code ({len(machine_code)} bytes): {hexdump}")


def parse_bindings(args):
    variables = {}
    for arg in args:
        name, _, value = arg.partition("=")
        variables[name] = int(value)
    return variables


def main():
    if len(sys.argv) > 1:
        run_one(sys.argv[1], parse_bindings(sys.argv[2:]))
        return

    print("x86-64 JIT REPL -- type an expression, or 'quit'")
    while True:
        try:
            line = input(">>> ")
        except EOFError:
            break
        if line.strip() in ("quit", "exit"):
            break
        if not line.strip():
            continue
        try:
            run_one(line, {})
        except (SyntaxError, ZeroDivisionError) as exc:
            print(f"error: {exc}")


if __name__ == "__main__":
    main()
