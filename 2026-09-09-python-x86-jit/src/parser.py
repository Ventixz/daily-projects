"""Recursive-descent parser.

    expr   := term (('+' | '-') term)*
    term   := factor (('*' | '/') factor)*
    factor := '-' factor | NUM | VAR | '(' expr ')'
"""

from .lexer import tokenize
from .ast_nodes import Num, Var, BinOp, Neg


class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0

    def _peek(self):
        return self.tokens[self.pos]

    def _advance(self):
        tok = self.tokens[self.pos]
        self.pos += 1
        return tok

    def _expect(self, kind):
        tok = self._advance()
        if tok.kind != kind:
            raise SyntaxError(f"expected {kind}, got {tok.kind}")
        return tok

    def parse(self):
        node = self.expr()
        self._expect("EOF")
        return node

    def expr(self):
        node = self.term()
        while self._peek().kind in ("PLUS", "MINUS"):
            op = "+" if self._advance().kind == "PLUS" else "-"
            node = BinOp(op, node, self.term())
        return node

    def term(self):
        node = self.factor()
        while self._peek().kind in ("STAR", "SLASH"):
            op = "*" if self._advance().kind == "STAR" else "/"
            node = BinOp(op, node, self.factor())
        return node

    def factor(self):
        tok = self._peek()
        if tok.kind == "MINUS":
            self._advance()
            return Neg(self.factor())
        if tok.kind == "NUM":
            self._advance()
            return Num(tok.value)
        if tok.kind == "VAR":
            self._advance()
            return Var(tok.value)
        if tok.kind == "LPAREN":
            self._advance()
            node = self.expr()
            self._expect("RPAREN")
            return node
        raise SyntaxError(f"unexpected token {tok.kind}")


def parse(text):
    return Parser(tokenize(text)).parse()
