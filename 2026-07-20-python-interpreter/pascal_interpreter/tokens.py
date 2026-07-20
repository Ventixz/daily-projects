"""Token types for the Pascal-subset language."""

from enum import Enum, auto


class TokenType(Enum):
    PLUS = auto()
    MINUS = auto()
    MUL = auto()
    FLOAT_DIV = auto()
    LPAREN = auto()
    RPAREN = auto()
    SEMI = auto()
    DOT = auto()
    COLON = auto()
    COMMA = auto()
    ASSIGN = auto()
    EQ = auto()
    NE = auto()
    LT = auto()
    GT = auto()
    LE = auto()
    GE = auto()

    ID = auto()
    INTEGER_CONST = auto()
    REAL_CONST = auto()
    STRING_CONST = auto()

    PROGRAM = auto()
    VAR = auto()
    INTEGER = auto()
    REAL = auto()
    BEGIN = auto()
    END = auto()
    IF = auto()
    THEN = auto()
    ELSE = auto()
    WHILE = auto()
    DO = auto()
    PRINT = auto()
    DIV = auto()
    MOD = auto()
    AND = auto()
    OR = auto()
    NOT = auto()
    TRUE = auto()
    FALSE = auto()

    EOF = auto()


class Token:
    __slots__ = ("type", "value", "line", "column")

    def __init__(self, type_, value, line, column):
        self.type = type_
        self.value = value
        self.line = line
        self.column = column

    def __repr__(self):
        return f"Token({self.type.name}, {self.value!r}, {self.line}:{self.column})"


# Keywords are matched case-insensitively; this maps the upper-cased
# spelling to its token type.
RESERVED_KEYWORDS = {
    "PROGRAM": TokenType.PROGRAM,
    "VAR": TokenType.VAR,
    "INTEGER": TokenType.INTEGER,
    "REAL": TokenType.REAL,
    "BEGIN": TokenType.BEGIN,
    "END": TokenType.END,
    "IF": TokenType.IF,
    "THEN": TokenType.THEN,
    "ELSE": TokenType.ELSE,
    "WHILE": TokenType.WHILE,
    "DO": TokenType.DO,
    "PRINT": TokenType.PRINT,
    "DIV": TokenType.DIV,
    "MOD": TokenType.MOD,
    "AND": TokenType.AND,
    "OR": TokenType.OR,
    "NOT": TokenType.NOT,
    "TRUE": TokenType.TRUE,
    "FALSE": TokenType.FALSE,
}
