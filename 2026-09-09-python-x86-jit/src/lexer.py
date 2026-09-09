"""Tokenizer for arithmetic expressions: integers, single-letter variables,
+ - * / ( ) and unary minus."""

from dataclasses import dataclass


@dataclass(frozen=True)
class Token:
    kind: str  # 'NUM', 'VAR', 'PLUS', 'MINUS', 'STAR', 'SLASH', 'LPAREN', 'RPAREN', 'EOF'
    value: object = None


_SINGLE = {
    "+": "PLUS",
    "-": "MINUS",
    "*": "STAR",
    "/": "SLASH",
    "(": "LPAREN",
    ")": "RPAREN",
}


def tokenize(text):
    tokens = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
            continue
        if c.isdigit():
            j = i
            while j < n and text[j].isdigit():
                j += 1
            tokens.append(Token("NUM", int(text[i:j])))
            i = j
            continue
        if c.isalpha():
            if not c.islower() or (i + 1 < n and text[i + 1].isalnum()):
                raise SyntaxError(f"variables must be a single lowercase letter, got {text[i:i+2]!r}")
            tokens.append(Token("VAR", c))
            i += 1
            continue
        if c in _SINGLE:
            tokens.append(Token(_SINGLE[c]))
            i += 1
            continue
        raise SyntaxError(f"unexpected character {c!r} at position {i}")
    tokens.append(Token("EOF"))
    return tokens
