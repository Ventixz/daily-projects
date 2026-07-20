from pathlib import Path

import pytest

from pascal_interpreter.cli import run
from pascal_interpreter.errors import RuntimeInterpreterError, SemanticError

EXAMPLES_DIR = Path(__file__).parent.parent / "examples"


def run_capturing(source):
    output = []
    memory = run(source, output=output.append)
    return memory, output


def test_arithmetic_and_operator_precedence():
    memory, _ = run_capturing("PROGRAM p; VAR x : INTEGER; BEGIN x := 2 + 3 * 4; END.")
    assert memory["x"] == 14


def test_real_division_vs_integer_div_and_mod():
    memory, _ = run_capturing(
        "PROGRAM p; VAR a, b, c : INTEGER; d : REAL; "
        "BEGIN a := 7 DIV 2; b := 7 MOD 2; d := 7 / 2; END."
    )
    assert memory["a"] == 3
    assert memory["b"] == 1
    assert memory["d"] == 3.5


def test_if_else_branches():
    memory, _ = run_capturing(
        "PROGRAM p; VAR x, y : INTEGER; "
        "BEGIN x := 5; IF x > 3 THEN y := 1 ELSE y := 0; END."
    )
    assert memory["y"] == 1


def test_while_loop_and_print(capsys=None):
    memory, output = run_capturing(
        "PROGRAM p; VAR i, sum : INTEGER; "
        "BEGIN i := 1; sum := 0; WHILE i <= 5 DO BEGIN sum := sum + i; i := i + 1; END; "
        "PRINT(sum); END."
    )
    assert memory["sum"] == 15
    assert output == [15]


def test_string_and_boolean_values():
    memory, output = run_capturing(
        "PROGRAM p; VAR ok : INTEGER; "
        "BEGIN ok := 1; PRINT('hi'); PRINT(TRUE AND NOT FALSE); END."
    )
    assert output == ["hi", True]


def test_undeclared_variable_raises_semantic_error():
    with pytest.raises(SemanticError):
        run_capturing("PROGRAM p; BEGIN x := 1; END.")


def test_duplicate_declaration_raises_semantic_error():
    with pytest.raises(SemanticError):
        run_capturing("PROGRAM p; VAR a : INTEGER; a : REAL; BEGIN END.")


def test_division_by_zero_raises_runtime_error():
    with pytest.raises(RuntimeInterpreterError):
        run_capturing("PROGRAM p; VAR x : INTEGER; BEGIN x := 1 DIV 0; END.")


def test_variable_used_before_assignment_raises_runtime_error():
    with pytest.raises(RuntimeInterpreterError):
        run_capturing("PROGRAM p; VAR x, y : INTEGER; BEGIN x := 1; y := y + 1; END.")


def test_factorial_example_end_to_end():
    source = (EXAMPLES_DIR / "factorial.pas").read_text()
    memory, output = run_capturing(source)
    assert memory["result"] == 3628800  # 10!
    assert output == [3628800]


def test_fizzbuzz_example_end_to_end():
    source = (EXAMPLES_DIR / "fizzbuzz.pas").read_text()
    _, output = run_capturing(source)
    assert output[:5] == [1, 2, "Fizz", 4, "Buzz"]
    assert output[14] == "FizzBuzz"  # 15th iteration
    assert len(output) == 20
