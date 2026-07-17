"""A minimal symbol table: tracks declared names and their types (built-in or variable)."""


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


class SymbolTable:
    def __init__(self):
        self._symbols = {}
        for name in ("INTEGER", "REAL"):
            self.define(BuiltinTypeSymbol(name))

    def __repr__(self):
        return f"SymbolTable({list(self._symbols)})"

    def define(self, symbol):
        self._symbols[symbol.name] = symbol

    def lookup(self, name):
        return self._symbols.get(name)
