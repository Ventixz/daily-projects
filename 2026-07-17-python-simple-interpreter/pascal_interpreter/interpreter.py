"""Tree-walking interpreter: evaluates the AST directly, no bytecode or codegen."""

from .node_visitor import NodeVisitor


class InterpreterError(Exception):
    pass


class Interpreter(NodeVisitor):
    def __init__(self, tree):
        self.tree = tree
        self.GLOBAL_SCOPE = {}

    def visit_Program(self, node):
        self.visit(node.block)

    def visit_Block(self, node):
        for declaration in node.declarations:
            self.visit(declaration)
        self.visit(node.compound_statement)

    def visit_VarDecl(self, node):
        pass  # declarations carry no runtime behavior; the semantic analyzer already validated them

    def visit_Type(self, node):
        pass

    def visit_Compound(self, node):
        for child in node.children:
            self.visit(child)

    def visit_NoOp(self, node):
        pass

    def visit_Assign(self, node):
        var_name = node.left.value
        self.GLOBAL_SCOPE[var_name] = self.visit(node.right)

    def visit_Var(self, node):
        var_name = node.value
        value = self.GLOBAL_SCOPE.get(var_name)
        if value is None:
            raise InterpreterError(f"Variable '{var_name}' referenced before assignment")
        return value

    def visit_BinOp(self, node):
        left = self.visit(node.left)
        right = self.visit(node.right)
        op_type = node.op.type
        if op_type == "PLUS":
            return left + right
        if op_type == "MINUS":
            return left - right
        if op_type == "MUL":
            return left * right
        if op_type == "INTEGER_DIV":
            return left // right
        if op_type == "FLOAT_DIV":
            return left / right
        raise InterpreterError(f"Unknown binary operator {op_type}")

    def visit_UnaryOp(self, node):
        value = self.visit(node.expr)
        if node.op.type == "MINUS":
            return -value
        return +value

    def visit_Num(self, node):
        return node.value

    def interpret(self):
        if self.tree is None:
            return {}
        self.visit(self.tree)
        return self.GLOBAL_SCOPE
