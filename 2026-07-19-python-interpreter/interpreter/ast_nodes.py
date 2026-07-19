class AST:
    """Marker base class — the Interpreter dispatches on subclass name."""


class Num(AST):
    def __init__(self, token):
        self.token = token
        self.value = token.value


class BinOp(AST):
    def __init__(self, left, op, right):
        self.left = left
        self.op = op
        self.right = right


class UnaryOp(AST):
    def __init__(self, op, expr):
        self.op = op
        self.expr = expr


class Var(AST):
    def __init__(self, token):
        self.token = token
        self.name = token.value


class Assign(AST):
    def __init__(self, name, expr):
        self.name = name
        self.expr = expr
