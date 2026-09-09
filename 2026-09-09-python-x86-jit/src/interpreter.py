"""A plain tree-walking evaluator over the same AST the JIT compiles.

Exists as the oracle the JIT is checked against, so its arithmetic has to
match what the generated machine code actually does bit-for-bit -- in
particular, division has to truncate toward zero like x86's IDIV, not
floor like Python's `//`.
"""

from .ast_nodes import Num, Var, BinOp, Neg


def truncating_div(a, b):
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


def evaluate(node, variables):
    if isinstance(node, Num):
        return node.value
    if isinstance(node, Var):
        return variables.get(node.name, 0)
    if isinstance(node, Neg):
        return -evaluate(node.operand, variables)
    if isinstance(node, BinOp):
        left = evaluate(node.left, variables)
        right = evaluate(node.right, variables)
        if node.op == "+":
            return left + right
        if node.op == "-":
            return left - right
        if node.op == "*":
            return left * right
        if node.op == "/":
            return truncating_div(left, right)
        raise ValueError(f"unknown operator {node.op!r}")
    raise TypeError(f"unknown node type {type(node).__name__}")
