"""AST node types produced by the parser and consumed by both the
reference interpreter and the JIT code generator."""

from dataclasses import dataclass


@dataclass(frozen=True)
class Num:
    value: int


@dataclass(frozen=True)
class Var:
    name: str


@dataclass(frozen=True)
class BinOp:
    op: str  # '+', '-', '*', '/'
    left: object
    right: object


@dataclass(frozen=True)
class Neg:
    operand: object
