"""Recursive-descent parser: Token stream -> AST.

Grammar (subset of Pascal):

    program             : PROGRAM variable SEMI block DOT
    block                : declarations compound_statement
    declarations          : (VAR (variable_declaration SEMI)+)*
    variable_declaration  : ID (COMMA ID)* COLON type_spec
    type_spec             : INTEGER | REAL
    compound_statement    : BEGIN statement_list END
    statement_list        : statement (SEMI statement)*
    statement             : compound_statement | assignment_statement | empty
    assignment_statement  : variable ASSIGN expr
    empty                 :
    expr                  : term ((PLUS | MINUS) term)*
    term                  : factor ((MUL | INTEGER_DIV | FLOAT_DIV) factor)*
    factor                : PLUS factor | MINUS factor
                            | INTEGER_CONST | REAL_CONST
                            | LPAREN expr RPAREN | variable
    variable              : ID
"""

from .ast_nodes import (
    Assign,
    BinOp,
    Block,
    Compound,
    NoOp,
    Num,
    Program,
    Type,
    UnaryOp,
    Var,
    VarDecl,
)
from .lexer import Lexer


class ParserError(Exception):
    pass


class Parser:
    def __init__(self, lexer: Lexer):
        self.lexer = lexer
        self.current_token = self.lexer.get_next_token()

    def error(self, message):
        token = self.current_token
        raise ParserError(f"Parser error at line {token.line}, column {token.column}: {message}")

    def eat(self, token_type):
        if self.current_token.type != token_type:
            self.error(f"expected {token_type} but found {self.current_token.type} ({self.current_token.value!r})")
        token = self.current_token
        self.current_token = self.lexer.get_next_token()
        return token

    # ---- grammar rules, one method per production ----

    def program(self):
        self.eat("PROGRAM")
        var_node = self.variable()
        prog_name = var_node.value
        self.eat("SEMI")
        block_node = self.block()
        node = Program(prog_name, block_node)
        self.eat("DOT")
        return node

    def block(self):
        declaration_nodes = self.declarations()
        compound_statement_node = self.compound_statement()
        return Block(declaration_nodes, compound_statement_node)

    def declarations(self):
        declarations = []
        while self.current_token.type == "VAR":
            self.eat("VAR")
            while self.current_token.type == "ID":
                declarations.extend(self.variable_declaration())
                self.eat("SEMI")
        return declarations

    def variable_declaration(self):
        var_nodes = [Var(self.current_token)]
        self.eat("ID")
        while self.current_token.type == "COMMA":
            self.eat("COMMA")
            var_nodes.append(Var(self.current_token))
            self.eat("ID")
        self.eat("COLON")
        type_node = self.type_spec()
        return [VarDecl(var_node, type_node) for var_node in var_nodes]

    def type_spec(self):
        token = self.current_token
        if token.type == "INTEGER":
            self.eat("INTEGER")
        else:
            self.eat("REAL")
        return Type(token)

    def compound_statement(self):
        self.eat("BEGIN")
        nodes = self.statement_list()
        self.eat("END")
        root = Compound()
        root.children.extend(nodes)
        return root

    def statement_list(self):
        node = self.statement()
        results = [node]
        while self.current_token.type == "SEMI":
            self.eat("SEMI")
            results.append(self.statement())
        return results

    def statement(self):
        if self.current_token.type == "BEGIN":
            return self.compound_statement()
        if self.current_token.type == "ID":
            return self.assignment_statement()
        return self.empty()

    def assignment_statement(self):
        left = self.variable()
        token = self.eat("ASSIGN")
        right = self.expr()
        return Assign(left, token, right)

    def variable(self):
        node = Var(self.current_token)
        self.eat("ID")
        return node

    def empty(self):
        return NoOp()

    def expr(self):
        node = self.term()
        while self.current_token.type in ("PLUS", "MINUS"):
            token = self.current_token
            self.eat(token.type)
            node = BinOp(left=node, op=token, right=self.term())
        return node

    def term(self):
        node = self.factor()
        while self.current_token.type in ("MUL", "INTEGER_DIV", "FLOAT_DIV"):
            token = self.current_token
            self.eat(token.type)
            node = BinOp(left=node, op=token, right=self.factor())
        return node

    def factor(self):
        token = self.current_token
        if token.type == "PLUS":
            self.eat("PLUS")
            return UnaryOp(token, self.factor())
        if token.type == "MINUS":
            self.eat("MINUS")
            return UnaryOp(token, self.factor())
        if token.type == "INTEGER_CONST":
            self.eat("INTEGER_CONST")
            return Num(token)
        if token.type == "REAL_CONST":
            self.eat("REAL_CONST")
            return Num(token)
        if token.type == "LPAREN":
            self.eat("LPAREN")
            node = self.expr()
            self.eat("RPAREN")
            return node
        return self.variable()

    def parse(self):
        node = self.program()
        if self.current_token.type != "EOF":
            self.error("expected EOF after program")
        return node
