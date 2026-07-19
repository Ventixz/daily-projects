import pytest

from interpreter.errors import EvalError
from interpreter.interpreter import Interpreter
from interpreter.lexer import Lexer
from interpreter.parser import Parser


def run(text, interpreter=None):
    interpreter = interpreter or Interpreter()
    tree = Parser(Lexer(text)).parse()
    return interpreter.interpret(tree)


@pytest.mark.parametrize(
    "expression, expected",
    [
        ("3 + 4", 7),
        ("3 + 4 * 2", 11),
        ("(3 + 4) * 2", 14),
        ("7 - 3 - 2", 2),           # left-associative subtraction
        ("2 * 3 + 4 * 5", 26),
        ("-3 + 5", 2),
        ("-(3 + 5)", -8),
        ("--5", 5),
        ("7 / 2", 3.5),
        ("3.5 * 2", 7.0),
    ],
)
def test_arithmetic(expression, expected):
    assert run(expression) == expected


def test_division_by_zero_raises():
    with pytest.raises(EvalError):
        run("1 / 0")


def test_undefined_variable_raises():
    with pytest.raises(EvalError):
        run("x + 1")


def test_assignment_persists_across_statements_in_shared_environment():
    interpreter = Interpreter()
    assert run("x = 5", interpreter) == 5
    assert run("y = x * 2", interpreter) == 10
    assert run("x + y", interpreter) == 15


def test_reassignment_overwrites():
    interpreter = Interpreter()
    run("x = 1", interpreter)
    run("x = x + 1", interpreter)
    assert run("x", interpreter) == 2


def test_separate_interpreters_do_not_share_state():
    a, b = Interpreter(), Interpreter()
    run("x = 1", a)
    with pytest.raises(EvalError):
        run("x", b)
