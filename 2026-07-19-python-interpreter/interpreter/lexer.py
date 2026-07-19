from .errors import LexerError

# Token type constants. Plain strings rather than an enum so tests can assert
# on them directly without importing a type.
INTEGER, FLOAT, IDENTIFIER = "INTEGER", "FLOAT", "IDENTIFIER"
PLUS, MINUS, MUL, DIV = "PLUS", "MINUS", "MUL", "DIV"
LPAREN, RPAREN, ASSIGN = "LPAREN", "RPAREN", "ASSIGN"
NEWLINE, EOF = "NEWLINE", "EOF"

_SINGLE_CHAR_TOKENS = {
    "+": PLUS,
    "-": MINUS,
    "*": MUL,
    "/": DIV,
    "(": LPAREN,
    ")": RPAREN,
    "=": ASSIGN,
}


class Token:
    def __init__(self, type_, value):
        self.type = type_
        self.value = value

    def __repr__(self):
        return f"Token({self.type}, {self.value!r})"

    def __eq__(self, other):
        return (
            isinstance(other, Token)
            and self.type == other.type
            and self.value == other.value
        )


class Lexer:
    """Turns a line of source text into a flat stream of Tokens.

    One lexer instance is scoped to a single line/statement — there is no
    token that spans a newline, which keeps the parser's grammar (one
    statement per line) simple.
    """

    def __init__(self, text):
        self.text = text
        self.pos = 0
        self.current_char = self.text[0] if text else None

    def error(self, message="Invalid character"):
        raise LexerError(message, self.pos)

    def advance(self):
        self.pos += 1
        self.current_char = (
            self.text[self.pos] if self.pos < len(self.text) else None
        )

    def peek(self):
        peek_pos = self.pos + 1
        return self.text[peek_pos] if peek_pos < len(self.text) else None

    def skip_whitespace(self):
        while self.current_char is not None and self.current_char in " \t":
            self.advance()

    def number(self):
        start = self.pos
        while self.current_char is not None and self.current_char.isdigit():
            self.advance()

        if self.current_char == "." and (self.peek() or "").isdigit():
            self.advance()
            while self.current_char is not None and self.current_char.isdigit():
                self.advance()
            return Token(FLOAT, float(self.text[start:self.pos]))

        return Token(INTEGER, int(self.text[start:self.pos]))

    def identifier(self):
        start = self.pos
        while self.current_char is not None and (
            self.current_char.isalnum() or self.current_char == "_"
        ):
            self.advance()
        return Token(IDENTIFIER, self.text[start:self.pos])

    def get_next_token(self):
        while self.current_char is not None:
            if self.current_char in " \t":
                self.skip_whitespace()
                continue

            if self.current_char.isdigit():
                return self.number()

            if self.current_char.isalpha() or self.current_char == "_":
                return self.identifier()

            if self.current_char in _SINGLE_CHAR_TOKENS:
                char = self.current_char
                self.advance()
                return Token(_SINGLE_CHAR_TOKENS[char], char)

            self.error(f"Unexpected character {self.current_char!r}")

        return Token(EOF, None)
