"""Hand-written tokenizer: turns source text into a stream of Tokens.

Reads the source one character at a time. There's no regex table driving
this on purpose -- each `peek`/`advance` pair makes the "how far ahead do I
need to look" question explicit, which is the whole point of writing a
lexer by hand instead of reaching for `re`.
"""

from .errors import LexerError
from .tokens import RESERVED_KEYWORDS, Token, TokenType

_SINGLE_CHAR_TOKENS = {
    "+": TokenType.PLUS,
    "-": TokenType.MINUS,
    "*": TokenType.MUL,
    "/": TokenType.FLOAT_DIV,
    "(": TokenType.LPAREN,
    ")": TokenType.RPAREN,
    ";": TokenType.SEMI,
    ".": TokenType.DOT,
    ",": TokenType.COMMA,
}


class Lexer:
    def __init__(self, text):
        self.text = text
        self.pos = 0
        self.line = 1
        self.column = 1
        self.current_char = self.text[0] if text else None

    def error(self, message):
        raise LexerError(message, self.line, self.column)

    def advance(self):
        if self.current_char == "\n":
            self.line += 1
            self.column = 1
        else:
            self.column += 1
        self.pos += 1
        self.current_char = self.text[self.pos] if self.pos < len(self.text) else None

    def peek(self):
        peek_pos = self.pos + 1
        return self.text[peek_pos] if peek_pos < len(self.text) else None

    def skip_whitespace(self):
        while self.current_char is not None and self.current_char.isspace():
            self.advance()

    def skip_comment(self):
        # `{ ... }` comments, Pascal-style. The opening `{` has already
        # been consumed by the caller.
        while self.current_char is not None and self.current_char != "}":
            self.advance()
        if self.current_char is None:
            self.error("unterminated comment")
        self.advance()  # consume closing '}'

    def number(self):
        start_line, start_col = self.line, self.column
        digits = ""
        while self.current_char is not None and self.current_char.isdigit():
            digits += self.current_char
            self.advance()

        if self.current_char == "." and (self.peek() or "").isdigit():
            digits += self.current_char
            self.advance()
            while self.current_char is not None and self.current_char.isdigit():
                digits += self.current_char
                self.advance()
            return Token(TokenType.REAL_CONST, float(digits), start_line, start_col)

        return Token(TokenType.INTEGER_CONST, int(digits), start_line, start_col)

    def string(self):
        start_line, start_col = self.line, self.column
        self.advance()  # consume opening quote
        value = ""
        while self.current_char is not None and self.current_char != "'":
            value += self.current_char
            self.advance()
        if self.current_char is None:
            self.error("unterminated string literal")
        self.advance()  # consume closing quote
        return Token(TokenType.STRING_CONST, value, start_line, start_col)

    def _id(self):
        start_line, start_col = self.line, self.column
        result = ""
        while self.current_char is not None and (
            self.current_char.isalnum() or self.current_char == "_"
        ):
            result += self.current_char
            self.advance()

        token_type = RESERVED_KEYWORDS.get(result.upper())
        if token_type is not None:
            return Token(token_type, result.upper(), start_line, start_col)
        return Token(TokenType.ID, result, start_line, start_col)

    def get_next_token(self):
        while self.current_char is not None:
            if self.current_char.isspace():
                self.skip_whitespace()
                continue

            if self.current_char == "{":
                self.advance()
                self.skip_comment()
                continue

            if self.current_char.isdigit():
                return self.number()

            if self.current_char == "'":
                return self.string()

            if self.current_char.isalpha() or self.current_char == "_":
                return self._id()

            line, col = self.line, self.column

            if self.current_char == ":" and self.peek() == "=":
                self.advance()
                self.advance()
                return Token(TokenType.ASSIGN, ":=", line, col)
            if self.current_char == ":":
                self.advance()
                return Token(TokenType.COLON, ":", line, col)
            if self.current_char == "=":
                self.advance()
                return Token(TokenType.EQ, "=", line, col)
            if self.current_char == "<" and self.peek() == ">":
                self.advance()
                self.advance()
                return Token(TokenType.NE, "<>", line, col)
            if self.current_char == "<" and self.peek() == "=":
                self.advance()
                self.advance()
                return Token(TokenType.LE, "<=", line, col)
            if self.current_char == "<":
                self.advance()
                return Token(TokenType.LT, "<", line, col)
            if self.current_char == ">" and self.peek() == "=":
                self.advance()
                self.advance()
                return Token(TokenType.GE, ">=", line, col)
            if self.current_char == ">":
                self.advance()
                return Token(TokenType.GT, ">", line, col)

            if self.current_char in _SINGLE_CHAR_TOKENS:
                token_type = _SINGLE_CHAR_TOKENS[self.current_char]
                char = self.current_char
                self.advance()
                return Token(token_type, char, line, col)

            self.error(f"unexpected character {self.current_char!r}")

        return Token(TokenType.EOF, None, self.line, self.column)

    def tokenize(self):
        """Drain the whole stream. Handy for tests; the parser instead
        pulls tokens one at a time via get_next_token()."""
        tokens = []
        while True:
            token = self.get_next_token()
            tokens.append(token)
            if token.type == TokenType.EOF:
                return tokens
