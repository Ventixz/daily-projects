"""Recursive-descent parser: Tokens -> AST.

Grammar (each method below implements the rule of the same name)::

    program              : PROGRAM variable SEMI block DOT
    block                : declarations compound_statement
    declarations         : (VAR (variable_declaration SEMI)+)?
    variable_declaration : ID (COMMA ID)* COLON type_spec
    type_spec             : INTEGER | REAL
    compound_statement    : BEGIN statement_list END
    statement_list        : statement (SEMI statement)*
    statement              : compound_statement
                            | assignment_statement
                            | if_statement
                            | while_statement
                            | print_statement
                            | empty
    assignment_statement    : variable ASSIGN expr
    if_statement             : IF expr THEN statement (ELSE statement)?
    while_statement           : WHILE expr DO statement
    print_statement            : PRINT LPAREN expr RPAREN
    variable                    : ID
    empty                        :

    expr        : simple_expr ((EQ | NE | LT | GT | LE | GE) simple_expr)?
    simple_expr : term ((PLUS | MINUS | OR) term)*
    term        : factor ((MUL | FLOAT_DIV | DIV | MOD | AND) factor)*
    factor      : PLUS factor
                | MINUS factor
                | NOT factor
                | INTEGER_CONST
                | REAL_CONST
                | STRING_CONST
                | TRUE
                | FALSE
                | LPAREN expr RPAREN
                | variable

The precedence climb (expr -> simple_expr -> term -> factor) is what makes
`2 + 3 * 4` parse as `2 + (3 * 4)`: each rule only calls down to a tighter-
binding rule, so a `*` two levels down always ends up nested inside a `+`
built one level up.
"""

from . import ast_nodes as ast
from .errors import ParserError
from .tokens import TokenType


class Parser:
    def __init__(self, lexer):
        self.lexer = lexer
        self.current_token = self.lexer.get_next_token()

    def error(self, message):
        raise ParserError(
            f"{message}, got {self.current_token.type.name} {self.current_token.value!r}",
            self.current_token.line,
            self.current_token.column,
        )

    def eat(self, token_type):
        if self.current_token.type != token_type:
            self.error(f"expected {token_type.name}")
        token = self.current_token
        self.current_token = self.lexer.get_next_token()
        return token

    # ---- grammar rules ----------------------------------------------

    def parse(self):
        node = self.program()
        if self.current_token.type != TokenType.EOF:
            self.error("expected end of input")
        return node

    def program(self):
        self.eat(TokenType.PROGRAM)
        var_node = self.variable()
        self.eat(TokenType.SEMI)
        block_node = self.block()
        self.eat(TokenType.DOT)
        return ast.Program(var_node.value, block_node)

    def block(self):
        declarations = self.declarations()
        compound_statement = self.compound_statement()
        return ast.Block(declarations, compound_statement)

    def declarations(self):
        declarations = []
        if self.current_token.type == TokenType.VAR:
            self.eat(TokenType.VAR)
            while self.current_token.type == TokenType.ID:
                declarations.extend(self.variable_declaration())
                self.eat(TokenType.SEMI)
        return declarations

    def variable_declaration(self):
        var_nodes = [ast.Var(self.eat(TokenType.ID))]
        while self.current_token.type == TokenType.COMMA:
            self.eat(TokenType.COMMA)
            var_nodes.append(ast.Var(self.eat(TokenType.ID)))
        self.eat(TokenType.COLON)
        type_node = self.type_spec()
        return [ast.VarDecl(var_node, type_node) for var_node in var_nodes]

    def type_spec(self):
        if self.current_token.type == TokenType.INTEGER:
            token = self.eat(TokenType.INTEGER)
        elif self.current_token.type == TokenType.REAL:
            token = self.eat(TokenType.REAL)
        else:
            self.error("expected a type (INTEGER or REAL)")
        return ast.Type(token)

    def compound_statement(self):
        self.eat(TokenType.BEGIN)
        nodes = self.statement_list()
        self.eat(TokenType.END)
        root = ast.Compound()
        root.children.extend(nodes)
        return root

    def statement_list(self):
        results = [self.statement()]
        while self.current_token.type == TokenType.SEMI:
            self.eat(TokenType.SEMI)
            results.append(self.statement())
        return results

    def statement(self):
        if self.current_token.type == TokenType.BEGIN:
            return self.compound_statement()
        if self.current_token.type == TokenType.ID:
            return self.assignment_statement()
        if self.current_token.type == TokenType.IF:
            return self.if_statement()
        if self.current_token.type == TokenType.WHILE:
            return self.while_statement()
        if self.current_token.type == TokenType.PRINT:
            return self.print_statement()
        return self.empty()

    def assignment_statement(self):
        left = self.variable()
        token = self.eat(TokenType.ASSIGN)
        right = self.expr()
        return ast.Assign(left, token, right)

    def if_statement(self):
        self.eat(TokenType.IF)
        condition = self.expr()
        self.eat(TokenType.THEN)
        then_branch = self.statement()
        else_branch = None
        if self.current_token.type == TokenType.ELSE:
            self.eat(TokenType.ELSE)
            else_branch = self.statement()
        return ast.IfStatement(condition, then_branch, else_branch)

    def while_statement(self):
        self.eat(TokenType.WHILE)
        condition = self.expr()
        self.eat(TokenType.DO)
        body = self.statement()
        return ast.WhileStatement(condition, body)

    def print_statement(self):
        token = self.eat(TokenType.PRINT)
        self.eat(TokenType.LPAREN)
        expr = self.expr()
        self.eat(TokenType.RPAREN)
        return ast.PrintStatement(token, expr)

    def variable(self):
        return ast.Var(self.eat(TokenType.ID))

    def empty(self):
        return ast.NoOp()

    _COMPARISON_OPS = {
        TokenType.EQ,
        TokenType.NE,
        TokenType.LT,
        TokenType.GT,
        TokenType.LE,
        TokenType.GE,
    }

    def expr(self):
        node = self.simple_expr()
        if self.current_token.type in self._COMPARISON_OPS:
            token = self.current_token
            self.eat(token.type)
            node = ast.BinOp(left=node, op=token, right=self.simple_expr())
        return node

    def simple_expr(self):
        node = self.term()
        while self.current_token.type in (TokenType.PLUS, TokenType.MINUS, TokenType.OR):
            token = self.current_token
            self.eat(token.type)
            node = ast.BinOp(left=node, op=token, right=self.term())
        return node

    _TERM_OPS = {TokenType.MUL, TokenType.FLOAT_DIV, TokenType.DIV, TokenType.MOD, TokenType.AND}

    def term(self):
        node = self.factor()
        while self.current_token.type in self._TERM_OPS:
            token = self.current_token
            self.eat(token.type)
            node = ast.BinOp(left=node, op=token, right=self.factor())
        return node

    def factor(self):
        token = self.current_token

        if token.type == TokenType.PLUS:
            self.eat(TokenType.PLUS)
            return ast.UnaryOp(token, self.factor())
        if token.type == TokenType.MINUS:
            self.eat(TokenType.MINUS)
            return ast.UnaryOp(token, self.factor())
        if token.type == TokenType.NOT:
            self.eat(TokenType.NOT)
            return ast.UnaryOp(token, self.factor())
        if token.type == TokenType.INTEGER_CONST:
            self.eat(TokenType.INTEGER_CONST)
            return ast.Num(token)
        if token.type == TokenType.REAL_CONST:
            self.eat(TokenType.REAL_CONST)
            return ast.Num(token)
        if token.type == TokenType.STRING_CONST:
            self.eat(TokenType.STRING_CONST)
            return ast.String(token)
        if token.type == TokenType.TRUE:
            self.eat(TokenType.TRUE)
            return ast.Bool(token)
        if token.type == TokenType.FALSE:
            self.eat(TokenType.FALSE)
            return ast.Bool(token)
        if token.type == TokenType.LPAREN:
            self.eat(TokenType.LPAREN)
            node = self.expr()
            self.eat(TokenType.RPAREN)
            return node
        if token.type == TokenType.ID:
            return self.variable()

        self.error("expected an expression")
