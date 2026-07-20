"""AST node types produced by the parser and walked by the analyzer/interpreter.

Every node carries the Token it was built from, so later passes can report
errors at the right source line/column instead of just "somewhere in your
program".
"""


class AST:
    pass


class Program(AST):
    def __init__(self, name, block):
        self.name = name
        self.block = block


class Block(AST):
    def __init__(self, declarations, compound_statement):
        self.declarations = declarations
        self.compound_statement = compound_statement


class VarDecl(AST):
    def __init__(self, var_node, type_node):
        self.var_node = var_node
        self.type_node = type_node


class Type(AST):
    def __init__(self, token):
        self.token = token
        self.value = token.value


class Compound(AST):
    """BEGIN ... END block: a list of statements."""

    def __init__(self):
        self.children = []


class Assign(AST):
    def __init__(self, left, op, right):
        self.left = left
        self.token = self.op = op
        self.right = right


class Var(AST):
    """A variable reference, e.g. the `x` in `x := x + 1`."""

    def __init__(self, token):
        self.token = token
        self.value = token.value


class NoOp(AST):
    """An empty statement, e.g. the second `;` in `x := 1;;`."""


class BinOp(AST):
    def __init__(self, left, op, right):
        self.left = left
        self.token = self.op = op
        self.right = right


class UnaryOp(AST):
    def __init__(self, op, expr):
        self.token = self.op = op
        self.expr = expr


class Num(AST):
    def __init__(self, token):
        self.token = token
        self.value = token.value


class String(AST):
    def __init__(self, token):
        self.token = token
        self.value = token.value


class Bool(AST):
    def __init__(self, token):
        self.token = token
        self.value = token.type.name == "TRUE"


class IfStatement(AST):
    def __init__(self, condition, then_branch, else_branch):
        self.condition = condition
        self.then_branch = then_branch
        self.else_branch = else_branch


class WhileStatement(AST):
    def __init__(self, condition, body):
        self.condition = condition
        self.body = body


class PrintStatement(AST):
    def __init__(self, token, expr):
        self.token = token
        self.expr = expr
