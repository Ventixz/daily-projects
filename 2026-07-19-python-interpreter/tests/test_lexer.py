import pytest

from interpreter.errors import LexerError
from interpreter.lexer import (
    ASSIGN, DIV, EOF, FLOAT, IDENTIFIER, INTEGER, LPAREN, MINUS, MUL, PLUS,
    RPAREN, Lexer, Token,
)


def tokens(text):
    lexer = Lexer(text)
    result = []
    while True:
        token = lexer.get_next_token()
        result.append(token)
        if token.type == EOF:
            return result


def test_single_digit():
    assert tokens("3") == [Token(INTEGER, 3), Token(EOF, None)]


def test_multi_digit_integer():
    assert tokens("314") == [Token(INTEGER, 314), Token(EOF, None)]


def test_float():
    assert tokens("3.14") == [Token(FLOAT, 3.14), Token(EOF, None)]


def test_trailing_dot_is_not_consumed_as_float():
    # "3." with nothing (or a non-digit) after the dot: the dot isn't part
    # of the number, so it should surface as its own (invalid) character.
    with pytest.raises(LexerError):
        tokens("3.")


def test_operators_and_parens():
    assert tokens("(1 + 2) * 3 / 4 - 5") == [
        Token(LPAREN, "("),
        Token(INTEGER, 1),
        Token(PLUS, "+"),
        Token(INTEGER, 2),
        Token(RPAREN, ")"),
        Token(MUL, "*"),
        Token(INTEGER, 3),
        Token(DIV, "/"),
        Token(INTEGER, 4),
        Token(MINUS, "-"),
        Token(INTEGER, 5),
        Token(EOF, None),
    ]


def test_identifier_and_assign():
    assert tokens("total_1 = 42") == [
        Token(IDENTIFIER, "total_1"),
        Token(ASSIGN, "="),
        Token(INTEGER, 42),
        Token(EOF, None),
    ]


def test_whitespace_is_insignificant():
    assert tokens("1+2") == tokens("  1   +    2  ")


def test_unexpected_character_raises():
    with pytest.raises(LexerError):
        tokens("1 & 2")
