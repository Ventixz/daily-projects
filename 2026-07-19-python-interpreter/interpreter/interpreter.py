from . import ast_nodes as ast
from .errors import EvalError
from .lexer import DIV, MINUS, MUL, PLUS


class Interpreter:
    """Tree-walking evaluator: one `visit_*` method per AST node type.

    `environment` is the variable store, passed in rather than owned here so
    a REPL can keep one Interpreter (and one environment) alive across
    lines — that's what makes `x = 5` on one line visible to `x * 2` on the
    next.
    """

    def __init__(self, environment=None):
        self.environment = environment if environment is not None else {}

    def visit(self, node):
        method_name = f"visit_{type(node).__name__}"
        visitor = getattr(self, method_name, self.generic_visit)
        return visitor(node)

    def generic_visit(self, node):
        raise EvalError(f"No visit_{type(node).__name__} method")

    def visit_Num(self, node):
        return node.value

    def visit_UnaryOp(self, node):
        value = self.visit(node.expr)
        return value if node.op.type == PLUS else -value

    def visit_BinOp(self, node):
        left = self.visit(node.left)
        right = self.visit(node.right)

        if node.op.type == PLUS:
            return left + right
        if node.op.type == MINUS:
            return left - right
        if node.op.type == MUL:
            return left * right
        if node.op.type == DIV:
            if right == 0:
                raise EvalError("Division by zero")
            return left / right

        raise EvalError(f"Unknown operator {node.op.type}")

    def visit_Var(self, node):
        try:
            return self.environment[node.name]
        except KeyError:
            raise EvalError(f"Undefined variable {node.name!r}") from None

    def visit_Assign(self, node):
        value = self.visit(node.expr)
        self.environment[node.name] = value
        return value

    def interpret(self, tree):
        return self.visit(tree)
