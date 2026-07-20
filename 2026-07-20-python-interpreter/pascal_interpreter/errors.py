"""Shared exception types, each tagged with a source position."""


class InterpreterError(Exception):
    def __init__(self, message, line=None, column=None):
        self.message = message
        self.line = line
        self.column = column
        location = f" (line {line}, col {column})" if line is not None else ""
        super().__init__(f"{message}{location}")


class LexerError(InterpreterError):
    pass


class ParserError(InterpreterError):
    pass


class SemanticError(InterpreterError):
    pass


class RuntimeInterpreterError(InterpreterError):
    pass
