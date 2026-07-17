"""Tokenizer for the Pascal subset: turns source text into a Token stream."""

RESERVED_KEYWORDS = {
    "PROGRAM": "PROGRAM",
    "VAR": "VAR",
    "DIV": "INTEGER_DIV",
    "INTEGER": "INTEGER",
    "REAL": "REAL",
    "BEGIN": "BEGIN",
    "END": "END",
}


class Token:
    def __init__(self, type_, value, line, column):
        self.type = type_
        self.value = value
        self.line = line
        self.column = column

    def __repr__(self):
        return f"Token({self.type}, {self.value!r}, line={self.line}, col={self.column})"


class LexerError(Exception):
    pass


class Lexer:
    def __init__(self, text):
        self.text = text
        self.pos = 0
        self.current_char = self.text[self.pos] if self.text else None
        self.line = 1
        self.column = 1

    def error(self, message):
        raise LexerError(f"Lexer error at line {self.line}, column {self.column}: {message}")

    def advance(self):
        if self.current_char == "\n":
            self.line += 1
            self.column = 0
        self.pos += 1
        if self.pos > len(self.text) - 1:
            self.current_char = None
        else:
            self.current_char = self.text[self.pos]
            self.column += 1

    def peek(self):
        peek_pos = self.pos + 1
        if peek_pos > len(self.text) - 1:
            return None
        return self.text[peek_pos]

    def skip_whitespace(self):
        while self.current_char is not None and self.current_char.isspace():
            self.advance()

    def skip_comment(self):
        while self.current_char is not None and self.current_char != "}":
            self.advance()
        if self.current_char is None:
            self.error("unterminated comment, expected '}'")
        self.advance()  # consume the closing '}'

    def number(self):
        line, column = self.line, self.column
        digits = []
        while self.current_char is not None and self.current_char.isdigit():
            digits.append(self.current_char)
            self.advance()

        if self.current_char == "." and (self.peek() or "").isdigit():
            digits.append(self.current_char)
            self.advance()
            while self.current_char is not None and self.current_char.isdigit():
                digits.append(self.current_char)
                self.advance()
            return Token("REAL_CONST", float("".join(digits)), line, column)

        return Token("INTEGER_CONST", int("".join(digits)), line, column)

    def _id(self):
        line, column = self.line, self.column
        chars = []
        while self.current_char is not None and (self.current_char.isalnum() or self.current_char == "_"):
            chars.append(self.current_char)
            self.advance()
        value = "".join(chars)
        token_type = RESERVED_KEYWORDS.get(value.upper())
        if token_type is not None:
            return Token(token_type, value.upper(), line, column)
        return Token("ID", value, line, column)

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

            if self.current_char.isalpha() or self.current_char == "_":
                return self._id()

            line, column = self.line, self.column

            if self.current_char == ":" and self.peek() == "=":
                self.advance()
                self.advance()
                return Token("ASSIGN", ":=", line, column)

            single_char_tokens = {
                "+": "PLUS",
                "-": "MINUS",
                "*": "MUL",
                "/": "FLOAT_DIV",
                "(": "LPAREN",
                ")": "RPAREN",
                ";": "SEMI",
                ":": "COLON",
                ",": "COMMA",
                ".": "DOT",
            }
            if self.current_char in single_char_tokens:
                char = self.current_char
                self.advance()
                return Token(single_char_tokens[char], char, line, column)

            self.error(f"unexpected character {self.current_char!r}")

        return Token("EOF", None, self.line, self.column)

    def tokenize(self):
        """Convenience for tests: return the full token list (including EOF)."""
        tokens = []
        while True:
            token = self.get_next_token()
            tokens.append(token)
            if token.type == "EOF":
                break
        return tokens
