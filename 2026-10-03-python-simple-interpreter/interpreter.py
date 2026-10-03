"""A tiny Pascal-subset interpreter: lexer -> parser (AST) -> tree-walking evaluator.

Follows the structure of Ruslan Spivak's "Let's Build A Simple Interpreter".
Supports PROGRAM, VAR declarations (INTEGER/REAL), nested BEGIN..END blocks,
assignment, + - * / DIV, unary +/-, parentheses, and { comments }.
"""
from dataclasses import dataclass

INTEGER, REAL, ID, ASSIGN, SEMI, DOT, COLON, COMMA = (
    "INTEGER", "REAL", "ID", "ASSIGN", "SEMI", "DOT", "COLON", "COMMA")
PLUS, MINUS, MUL, FLOAT_DIV, LPAREN, RPAREN, EOF = (
    "PLUS", "MINUS", "MUL", "FLOAT_DIV", "LPAREN", "RPAREN", "EOF")
INTEGER_CONST, REAL_CONST = "INTEGER_CONST", "REAL_CONST"

KEYWORDS = {"PROGRAM", "VAR", "DIV", "INTEGER", "REAL", "BEGIN", "END"}
SINGLE = {";": SEMI, ".": DOT, ":": COLON, ",": COMMA, "+": PLUS, "-": MINUS,
          "*": MUL, "/": FLOAT_DIV, "(": LPAREN, ")": RPAREN}


class InterpreterError(Exception):
    pass


@dataclass
class Token:
    type: str
    value: object


class Lexer:
    def __init__(self, text):
        self.text, self.pos = text, 0

    def _peek(self, n=1):
        i = self.pos + n
        return self.text[i] if i < len(self.text) else None

    def _number(self):
        start = self.pos
        while self.pos < len(self.text) and self.text[self.pos].isdigit():
            self.pos += 1
        if self.pos < len(self.text) and self.text[self.pos] == ".":
            self.pos += 1
            while self.pos < len(self.text) and self.text[self.pos].isdigit():
                self.pos += 1
            return Token(REAL_CONST, float(self.text[start:self.pos]))
        return Token(INTEGER_CONST, int(self.text[start:self.pos]))

    def _word(self):
        start = self.pos
        while self.pos < len(self.text) and (self.text[self.pos].isalnum() or self.text[self.pos] == "_"):
            self.pos += 1
        word = self.text[start:self.pos].upper()  # Pascal is case-insensitive
        return Token(word, word) if word in KEYWORDS else Token(ID, word)

    def next_token(self):
        while self.pos < len(self.text):
            c = self.text[self.pos]
            if c.isspace():
                self.pos += 1
            elif c == "{":
                end = self.text.find("}", self.pos)
                if end == -1:
                    raise InterpreterError("unterminated comment")
                self.pos = end + 1
            elif c.isdigit():
                return self._number()
            elif c.isalpha() or c == "_":
                return self._word()
            elif c == ":" and self._peek() == "=":
                self.pos += 2
                return Token(ASSIGN, ":=")
            elif c in SINGLE:
                self.pos += 1
                return Token(SINGLE[c], c)
            else:
                raise InterpreterError(f"unexpected character {c!r} at {self.pos}")
        return Token(EOF, None)


# --- AST ---------------------------------------------------------------
@dataclass
class Program: name: str; block: object
@dataclass
class Block: decls: list; body: object
@dataclass
class VarDecl: name: str; type: str
@dataclass
class Compound: children: list
@dataclass
class Assign: name: str; expr: object
@dataclass
class Var: name: str
@dataclass
class Num: value: object
@dataclass
class BinOp: op: str; left: object; right: object
@dataclass
class UnaryOp: op: str; expr: object
class NoOp: pass


class Parser:
    def __init__(self, lexer):
        self.lexer = lexer
        self.tok = lexer.next_token()

    def eat(self, type_):
        if self.tok.type != type_:
            raise InterpreterError(f"expected {type_}, got {self.tok.type}")
        self.tok = self.lexer.next_token()

    def program(self):
        self.eat("PROGRAM")
        name = self.tok.value
        self.eat(ID)
        self.eat(SEMI)
        block = self.block()
        self.eat(DOT)
        self.eat(EOF)
        return Program(name, block)

    def block(self):
        decls = []
        if self.tok.type == "VAR":
            self.eat("VAR")
            while self.tok.type == ID:
                names = [self.tok.value]
                self.eat(ID)
                while self.tok.type == COMMA:
                    self.eat(COMMA)
                    names.append(self.tok.value)
                    self.eat(ID)
                self.eat(COLON)
                typ = self.tok.type
                if typ not in (INTEGER, REAL):
                    raise InterpreterError("expected INTEGER or REAL")
                self.eat(typ)
                self.eat(SEMI)
                decls += [VarDecl(n, typ) for n in names]
        return Block(decls, self.compound())

    def compound(self):
        self.eat("BEGIN")
        nodes = [self.statement()]
        while self.tok.type == SEMI:
            self.eat(SEMI)
            nodes.append(self.statement())
        self.eat("END")
        return Compound(nodes)

    def statement(self):
        if self.tok.type == "BEGIN":
            return self.compound()
        if self.tok.type == ID:
            name = self.tok.value
            self.eat(ID)
            self.eat(ASSIGN)
            return Assign(name, self.expr())
        return NoOp()

    def expr(self):
        node = self.term()
        while self.tok.type in (PLUS, MINUS):
            op = self.tok.type
            self.eat(op)
            node = BinOp(op, node, self.term())
        return node

    def term(self):
        node = self.factor()
        while self.tok.type in (MUL, "DIV", FLOAT_DIV):
            op = self.tok.type
            self.eat(op)
            node = BinOp(op, node, self.factor())
        return node

    def factor(self):
        t = self.tok
        if t.type in (PLUS, MINUS):
            self.eat(t.type)
            return UnaryOp(t.type, self.factor())
        if t.type in (INTEGER_CONST, REAL_CONST):
            self.eat(t.type)
            return Num(t.value)
        if t.type == LPAREN:
            self.eat(LPAREN)
            node = self.expr()
            self.eat(RPAREN)
            return node
        if t.type == ID:
            self.eat(ID)
            return Var(t.value)
        raise InterpreterError(f"unexpected token {t.type}")


class Interpreter:
    """Walks the AST; `scope` maps variable name -> value after run()."""

    def __init__(self, text):
        self.tree = Parser(Lexer(text)).program()
        self.types = {}
        self.scope = {}

    def run(self):
        self.visit(self.tree)
        return self.scope

    def visit(self, node):
        return getattr(self, "v_" + type(node).__name__)(node)

    def v_Program(self, n): self.visit(n.block)
    def v_NoOp(self, n): pass

    def v_Block(self, n):
        for d in n.decls:
            self.visit(d)
        self.visit(n.body)

    def v_VarDecl(self, n):
        if n.name in self.types:
            raise InterpreterError(f"duplicate declaration of {n.name}")
        self.types[n.name] = n.type

    def v_Compound(self, n):
        for c in n.children:
            self.visit(c)

    def v_Assign(self, n):
        if n.name not in self.types:
            raise InterpreterError(f"undeclared variable {n.name}")
        value = self.visit(n.expr)
        if self.types[n.name] == INTEGER:
            if isinstance(value, float):
                raise InterpreterError(f"cannot assign REAL to INTEGER variable {n.name}")
        else:
            value = float(value)
        self.scope[n.name] = value

    def v_Var(self, n):
        if n.name not in self.scope:
            raise InterpreterError(f"variable {n.name} used before assignment")
        return self.scope[n.name]

    def v_Num(self, n): return n.value

    def v_UnaryOp(self, n):
        v = self.visit(n.expr)
        return +v if n.op == PLUS else -v

    def v_BinOp(self, n):
        a, b = self.visit(n.left), self.visit(n.right)
        if n.op == PLUS: return a + b
        if n.op == MINUS: return a - b
        if n.op == MUL: return a * b
        if b == 0:
            raise InterpreterError("division by zero")
        if n.op == FLOAT_DIV: return a / b
        if isinstance(a, float) or isinstance(b, float):
            raise InterpreterError("DIV requires integer operands")
        return int(a / b)  # Pascal DIV truncates toward zero


def interpret(text):
    return Interpreter(text).run()


if __name__ == "__main__":
    import sys
    src = open(sys.argv[1]).read() if len(sys.argv) > 1 else sys.stdin.read()
    for k, v in sorted(interpret(src).items()):
        print(f"{k} = {v}")
