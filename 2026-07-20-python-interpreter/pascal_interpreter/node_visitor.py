"""Shared dispatch-by-node-type-name base class.

Both the semantic analyzer and the interpreter need to "do something
different for each AST node type" -- this is the classic visitor pattern
implemented via `getattr` instead of a big if/elif chain, so adding a new
node type only means adding a new `visit_Foo` method, not touching every
existing dispatcher.
"""


class NodeVisitor:
    def visit(self, node):
        method_name = "visit_" + type(node).__name__
        visitor = getattr(self, method_name, self.generic_visit)
        return visitor(node)

    def generic_visit(self, node):
        raise Exception(f"no visit_{type(node).__name__} method defined")
