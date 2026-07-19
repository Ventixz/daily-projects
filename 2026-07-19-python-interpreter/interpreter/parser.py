from . import ast_nodes as ast
from .errors import ParserError
from .lexer import (
    ASSIGN, DIV, EOF, FLOAT, IDENTIFIER, INTEGER, LPAREN, MINUS, MUL, PLUS,
    RPAREN,
)


class Parser:
    """Recursive-descent parser for one line of input.

    Grammar (lowest to highest precedence):

        statement  : assignment | expr
        assignment : IDENTIFIER ASSIGN expr
        expr       : term ((PLUS | MINUS) term)*
        term       : factor ((MUL | DIV) factor)*
        factor     : (PLUS | MINUS) factor
                   | INTEGER | FLOAT | IDENTIFIER
                   | LPAREN expr RPAREN

    `expr`/`term` are the classic left-recursion-as-a-loop encoding of
    left-associative binary operators; `factor` recursing into itself on a
    leading +/- is what makes unary minus chain correctly (`--5`, `-(-5)`).
    """

    def __init__(self, lexer):
        self.lexer = lexer
        self.current_token = self.lexer.get_next_token()

    def error(self, message="Unexpected token"):
        raise ParserError(message, self.current_token)

    def eat(self, token_type):
        if self.current_token.type != token_type:
            self.error(f"Expected {token_type}")
        token = self.current_token
        self.current_token = self.lexer.get_next_token()
        return token

    def _next_is_assign(self):
        # Only way to tell "x = 1" (assignment) from "x + 1" (expression)
        # apart at the IDENTIFIER token is a one-token lookahead. The lexer
        # has no buffering, so snapshot/restore its two bits of state around
        # the peek.
        saved_pos, saved_char = self.lexer.pos, self.lexer.current_char
        peeked = self.lexer.get_next_token()
        self.lexer.pos, self.lexer.current_char = saved_pos, saved_char
        return peeked.type == ASSIGN

    def factor(self):
        token = self.current_token

        if token.type in (PLUS, MINUS):
            self.eat(token.type)
            return ast.UnaryOp(token, self.factor())

        if token.type in (INTEGER, FLOAT):
            self.eat(token.type)
            return ast.Num(token)

        if token.type == LPAREN:
            self.eat(LPAREN)
            node = self.expr()
            self.eat(RPAREN)
            return node

        if token.type == IDENTIFIER:
            self.eat(IDENTIFIER)
            return ast.Var(token)

        self.error("Expected a number, identifier, or '('")

    def term(self):
        node = self.factor()
        while self.current_token.type in (MUL, DIV):
            op = self.eat(self.current_token.type)
            node = ast.BinOp(left=node, op=op, right=self.factor())
        return node

    def expr(self):
        node = self.term()
        while self.current_token.type in (PLUS, MINUS):
            op = self.eat(self.current_token.type)
            node = ast.BinOp(left=node, op=op, right=self.term())
        return node

    def statement(self):
        if self.current_token.type == IDENTIFIER and self._next_is_assign():
            name = self.current_token.value
            self.eat(IDENTIFIER)
            self.eat(ASSIGN)
            return ast.Assign(name, self.expr())
        return self.expr()

    def parse(self):
        node = self.statement()
        if self.current_token.type != EOF:
            self.error("Unexpected trailing input")
        return node
