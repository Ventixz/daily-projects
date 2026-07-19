import pytest

from interpreter import ast_nodes as ast
from interpreter.errors import ParserError
from interpreter.lexer import Lexer
from interpreter.parser import Parser


def parse(text):
    return Parser(Lexer(text)).parse()


def test_single_number():
    node = parse("42")
    assert isinstance(node, ast.Num)
    assert node.value == 42


def test_binop_precedence_shape():
    # "1 + 2 * 3" must attach the multiplication as the RHS of the addition,
    # not the other way round — that's what gives multiplication higher
    # precedence without an explicit precedence table.
    node = parse("1 + 2 * 3")
    assert isinstance(node, ast.BinOp)
    assert node.op.type == "PLUS"
    assert isinstance(node.left, ast.Num) and node.left.value == 1
    assert isinstance(node.right, ast.BinOp)
    assert node.right.op.type == "MUL"


def test_parens_override_precedence():
    node = parse("(1 + 2) * 3")
    assert isinstance(node, ast.BinOp)
    assert node.op.type == "MUL"
    assert isinstance(node.left, ast.BinOp)
    assert node.left.op.type == "PLUS"


def test_left_associativity():
    # "1 - 2 - 3" must parse as (1 - 2) - 3, not 1 - (2 - 3) — those give
    # different answers (-4 vs 2).
    node = parse("1 - 2 - 3")
    assert isinstance(node, ast.BinOp) and node.op.type == "MINUS"
    assert isinstance(node.left, ast.BinOp) and node.left.op.type == "MINUS"
    assert isinstance(node.right, ast.Num) and node.right.value == 3


def test_unary_minus_chain():
    node = parse("--5")
    assert isinstance(node, ast.UnaryOp)
    assert isinstance(node.expr, ast.UnaryOp)
    assert node.expr.expr.value == 5


def test_assignment_vs_expression():
    assign = parse("x = 1 + 2")
    assert isinstance(assign, ast.Assign)
    assert assign.name == "x"
    assert isinstance(assign.expr, ast.BinOp)

    expr = parse("x + 2")
    assert isinstance(expr, ast.BinOp)
    assert isinstance(expr.left, ast.Var)


def test_missing_operand_raises():
    with pytest.raises(ParserError):
        parse("1 +")


def test_unbalanced_paren_raises():
    with pytest.raises(ParserError):
        parse("(1 + 2")


def test_trailing_tokens_raise():
    with pytest.raises(ParserError):
        parse("1 2")
