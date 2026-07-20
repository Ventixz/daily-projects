import pytest

from pascal_interpreter import ast_nodes as ast
from pascal_interpreter.errors import ParserError
from pascal_interpreter.lexer import Lexer
from pascal_interpreter.parser import Parser


def parse(source):
    return Parser(Lexer(source)).parse()


def test_parses_minimal_program():
    tree = parse("PROGRAM p; BEGIN END.")
    assert isinstance(tree, ast.Program)
    assert tree.name == "p"
    assert tree.block.declarations == []
    # An empty BEGIN...END still yields one (empty) statement, since the
    # grammar's statement_list always parses at least one `statement`.
    children = tree.block.compound_statement.children
    assert len(children) == 1
    assert isinstance(children[0], ast.NoOp)


def test_parses_variable_declarations():
    tree = parse("PROGRAM p; VAR a, b : INTEGER; c : REAL; BEGIN END.")
    names = [decl.var_node.value for decl in tree.block.declarations]
    types = [decl.type_node.value for decl in tree.block.declarations]
    assert names == ["a", "b", "c"]
    assert types == ["INTEGER", "INTEGER", "REAL"]


def test_binary_operator_precedence():
    # 2 + 3 * 4 should parse as 2 + (3 * 4), not (2 + 3) * 4.
    tree = parse("PROGRAM p; VAR x : INTEGER; BEGIN x := 2 + 3 * 4; END.")
    assign = tree.block.compound_statement.children[0]
    root = assign.right
    assert isinstance(root, ast.BinOp)
    assert root.op.value == "+"
    assert isinstance(root.left, ast.Num) and root.left.value == 2
    assert isinstance(root.right, ast.BinOp)
    assert root.right.op.value == "*"


def test_if_else_and_while_parse():
    tree = parse(
        """
        PROGRAM p;
        VAR i : INTEGER;
        BEGIN
           IF i > 0 THEN
              i := i - 1
           ELSE
              i := 0;
           WHILE i < 10 DO
              i := i + 1;
        END.
        """
    )
    # A trailing `;` before END parses one extra, empty statement -- that's
    # standard Pascal-grammar behavior, not a bug.
    if_stmt, while_stmt, trailing = tree.block.compound_statement.children
    assert isinstance(trailing, ast.NoOp)
    assert isinstance(if_stmt, ast.IfStatement)
    assert if_stmt.else_branch is not None
    assert isinstance(while_stmt, ast.WhileStatement)


def test_missing_semicolon_raises_parser_error():
    with pytest.raises(ParserError):
        parse("PROGRAM p; VAR a : INTEGER BEGIN END.")


def test_missing_end_raises_parser_error():
    with pytest.raises(ParserError):
        parse("PROGRAM p; BEGIN .")
