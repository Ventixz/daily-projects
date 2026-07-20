"""Semantic analysis: a separate pass over the AST, before evaluation.

This is the difference between "your program is broken" and "line 7 refers
to a variable you never declared" -- catching it here, walking the tree
once with a symbol table, means the interpreter itself can assume every
`Var` it evaluates is already known to exist. Real compilers do the same
split: parse, then check, then run.
"""

from . import ast_nodes as ast
from .errors import SemanticError
from .node_visitor import NodeVisitor


class Symbol:
    def __init__(self, name, type_=None):
        self.name = name
        self.type = type_


class BuiltinTypeSymbol(Symbol):
    def __repr__(self):
        return self.name


class VarSymbol(Symbol):
    def __repr__(self):
        return f"<{self.name}: {self.type}>"


class ScopedSymbolTable:
    def __init__(self, scope_name, scope_level, enclosing_scope=None):
        self._symbols = {}
        self.scope_name = scope_name
        self.scope_level = scope_level
        self.enclosing_scope = enclosing_scope
        for type_name in ("INTEGER", "REAL"):
            self.insert(BuiltinTypeSymbol(type_name))

    def insert(self, symbol):
        self._symbols[symbol.name] = symbol

    def lookup(self, name, current_scope_only=False):
        symbol = self._symbols.get(name)
        if symbol is not None:
            return symbol
        if current_scope_only or self.enclosing_scope is None:
            return None
        return self.enclosing_scope.lookup(name)

    def __repr__(self):
        lines = [f"Scope: {self.scope_name} (level {self.scope_level})"]
        for name, symbol in self._symbols.items():
            lines.append(f"  {name}: {symbol!r}")
        return "\n".join(lines)


class SemanticAnalyzer(NodeVisitor):
    def __init__(self):
        self.current_scope = None

    def analyze(self, tree):
        self.visit(tree)

    def visit_Program(self, node):
        global_scope = ScopedSymbolTable(scope_name="global", scope_level=1)
        self.current_scope = global_scope
        self.visit(node.block)
        self.current_scope = self.current_scope.enclosing_scope

    def visit_Block(self, node):
        for declaration in node.declarations:
            self.visit(declaration)
        self.visit(node.compound_statement)

    def visit_VarDecl(self, node):
        type_name = node.type_node.value
        type_symbol = self.current_scope.lookup(type_name)
        var_name = node.var_node.value

        if self.current_scope.lookup(var_name, current_scope_only=True) is not None:
            raise SemanticError(
                f"duplicate declaration of variable '{var_name}'",
                node.var_node.token.line,
                node.var_node.token.column,
            )

        self.current_scope.insert(VarSymbol(var_name, type_symbol))

    def visit_Compound(self, node):
        for child in node.children:
            self.visit(child)

    def visit_Assign(self, node):
        self.visit(node.right)
        self.visit(node.left)

    def visit_Var(self, node):
        var_name = node.value
        if self.current_scope.lookup(var_name) is None:
            raise SemanticError(
                f"undeclared variable '{var_name}'", node.token.line, node.token.column
            )

    def visit_BinOp(self, node):
        self.visit(node.left)
        self.visit(node.right)

    def visit_UnaryOp(self, node):
        self.visit(node.expr)

    def visit_IfStatement(self, node):
        self.visit(node.condition)
        self.visit(node.then_branch)
        if node.else_branch is not None:
            self.visit(node.else_branch)

    def visit_WhileStatement(self, node):
        self.visit(node.condition)
        self.visit(node.body)

    def visit_PrintStatement(self, node):
        self.visit(node.expr)

    def visit_Num(self, node):
        pass

    def visit_String(self, node):
        pass

    def visit_Bool(self, node):
        pass

    def visit_NoOp(self, node):
        pass
