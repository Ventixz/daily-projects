"""Static checks that run on the AST before interpretation: catches undeclared
variables and duplicate declarations without executing a single statement.
"""

from .node_visitor import NodeVisitor
from .symbols import SymbolTable, VarSymbol


class SemanticError(Exception):
    pass


class SemanticAnalyzer(NodeVisitor):
    def __init__(self):
        self.symtab = SymbolTable()

    def analyze(self, tree):
        self.visit(tree)
        return self.symtab

    def visit_Program(self, node):
        self.visit(node.block)

    def visit_Block(self, node):
        for declaration in node.declarations:
            self.visit(declaration)
        self.visit(node.compound_statement)

    def visit_VarDecl(self, node):
        type_name = node.type_node.value
        type_symbol = self.symtab.lookup(type_name)
        var_name = node.var_node.value
        if self.symtab.lookup(var_name) is not None:
            raise SemanticError(f"Duplicate declaration of variable '{var_name}'")
        self.symtab.define(VarSymbol(var_name, type_symbol))

    def visit_Compound(self, node):
        for child in node.children:
            self.visit(child)

    def visit_Assign(self, node):
        var_name = node.left.value
        if self.symtab.lookup(var_name) is None:
            raise SemanticError(f"Assignment to undeclared variable '{var_name}'")
        self.visit(node.right)

    def visit_Var(self, node):
        var_name = node.value
        if self.symtab.lookup(var_name) is None:
            raise SemanticError(f"Undeclared variable '{var_name}'")

    def visit_BinOp(self, node):
        self.visit(node.left)
        self.visit(node.right)

    def visit_UnaryOp(self, node):
        self.visit(node.expr)

    def visit_Num(self, node):
        pass

    def visit_NoOp(self, node):
        pass
