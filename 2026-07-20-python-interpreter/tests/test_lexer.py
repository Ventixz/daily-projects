import pytest

from pascal_interpreter.errors import LexerError
from pascal_interpreter.lexer import Lexer
from pascal_interpreter.tokens import TokenType


def token_types(source):
    return [t.type for t in Lexer(source).tokenize()]


def test_tokenizes_program_skeleton():
    types = token_types("PROGRAM p; BEGIN END.")
    assert types == [
        TokenType.PROGRAM,
        TokenType.ID,
        TokenType.SEMI,
        TokenType.BEGIN,
        TokenType.END,
        TokenType.DOT,
        TokenType.EOF,
    ]


def test_keywords_are_case_insensitive():
    assert token_types("program")[0] == TokenType.PROGRAM
    assert token_types("Program")[0] == TokenType.PROGRAM
    assert token_types("PROGRAM")[0] == TokenType.PROGRAM


def test_integer_and_real_constants():
    lexer = Lexer("42 3.14")
    int_tok = lexer.get_next_token()
    real_tok = lexer.get_next_token()
    assert (int_tok.type, int_tok.value) == (TokenType.INTEGER_CONST, 42)
    assert (real_tok.type, real_tok.value) == (TokenType.REAL_CONST, 3.14)


def test_string_literal():
    lexer = Lexer("'hello world'")
    tok = lexer.get_next_token()
    assert (tok.type, tok.value) == (TokenType.STRING_CONST, "hello world")


def test_two_character_operators():
    types = token_types(":= <> <= >=")
    assert types == [TokenType.ASSIGN, TokenType.NE, TokenType.LE, TokenType.GE, TokenType.EOF]


def test_comments_are_skipped():
    types = token_types("{ this is a comment } BEGIN END.")
    assert types == [TokenType.BEGIN, TokenType.END, TokenType.DOT, TokenType.EOF]


def test_unexpected_character_raises_lexer_error():
    with pytest.raises(LexerError):
        Lexer("@").tokenize()


def test_unterminated_string_raises_lexer_error():
    with pytest.raises(LexerError):
        Lexer("'unterminated").tokenize()


def test_error_reports_line_and_column():
    with pytest.raises(LexerError) as exc_info:
        Lexer("BEGIN\n  @\nEND.").tokenize()
    assert exc_info.value.line == 2
