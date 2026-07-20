"""Tree-walking evaluator: walks the AST the semantic analyzer already
checked, and actually produces values.

No bytecode, no compilation step -- `visit(node)` on a BinOp just calls
`visit` on its two children and combines the results. It's the simplest
thing that can execute a program, at the cost of being slower than a
bytecode VM for anything long-running.
"""

from .errors import RuntimeInterpreterError
from .node_visitor import NodeVisitor
from .tokens import TokenType


class Interpreter(NodeVisitor):
    def __init__(self, output=print):
        self.GLOBAL_MEMORY = {}
        self._output = output

    def interpret(self, tree):
        self.visit(tree)
        return self.GLOBAL_MEMORY

    def visit_Program(self, node):
        self.visit(node.block)

    def visit_Block(self, node):
        for declaration in node.declarations:
            self.visit(declaration)
        self.visit(node.compound_statement)

    def visit_VarDecl(self, node):
        # Declarations only affect the symbol table (already checked by
        # the semantic analyzer); nothing to evaluate here.
        pass

    def visit_Type(self, node):
        pass

    def visit_Compound(self, node):
        for child in node.children:
            self.visit(child)

    def visit_Assign(self, node):
        self.GLOBAL_MEMORY[node.left.value] = self.visit(node.right)

    def visit_Var(self, node):
        value = self.GLOBAL_MEMORY.get(node.value)
        if value is None:
            raise RuntimeInterpreterError(
                f"variable '{node.value}' used before assignment",
                node.token.line,
                node.token.column,
            )
        return value

    def visit_NoOp(self, node):
        pass

    def visit_IfStatement(self, node):
        if self.visit(node.condition):
            self.visit(node.then_branch)
        elif node.else_branch is not None:
            self.visit(node.else_branch)

    def visit_WhileStatement(self, node):
        while self.visit(node.condition):
            self.visit(node.body)

    def visit_PrintStatement(self, node):
        self._output(self.visit(node.expr))

    def visit_Num(self, node):
        return node.value

    def visit_String(self, node):
        return node.value

    def visit_Bool(self, node):
        return node.value

    def visit_UnaryOp(self, node):
        value = self.visit(node.expr)
        if node.op.type == TokenType.PLUS:
            return +value
        if node.op.type == TokenType.MINUS:
            return -value
        if node.op.type == TokenType.NOT:
            return not value
        raise RuntimeInterpreterError(
            f"unsupported unary operator {node.op.value}", node.op.line, node.op.column
        )

    def visit_BinOp(self, node):
        left = self.visit(node.left)
        right = self.visit(node.right)
        op = node.op.type

        if op == TokenType.PLUS:
            return left + right
        if op == TokenType.MINUS:
            return left - right
        if op == TokenType.MUL:
            return left * right
        if op == TokenType.FLOAT_DIV:
            if right == 0:
                raise RuntimeInterpreterError(
                    "division by zero", node.op.line, node.op.column
                )
            return left / right
        if op == TokenType.DIV:
            if right == 0:
                raise RuntimeInterpreterError(
                    "integer division by zero", node.op.line, node.op.column
                )
            return int(left) // int(right)
        if op == TokenType.MOD:
            if right == 0:
                raise RuntimeInterpreterError("mod by zero", node.op.line, node.op.column)
            return left % right
        if op == TokenType.AND:
            return bool(left) and bool(right)
        if op == TokenType.OR:
            return bool(left) or bool(right)
        if op == TokenType.EQ:
            return left == right
        if op == TokenType.NE:
            return left != right
        if op == TokenType.LT:
            return left < right
        if op == TokenType.GT:
            return left > right
        if op == TokenType.LE:
            return left <= right
        if op == TokenType.GE:
            return left >= right

        raise RuntimeInterpreterError(
            f"unsupported binary operator {node.op.value}", node.op.line, node.op.column
        )
