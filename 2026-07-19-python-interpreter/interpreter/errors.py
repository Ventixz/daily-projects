class InterpreterError(Exception):
    """Base class for every error this package raises."""


class LexerError(InterpreterError):
    def __init__(self, message, pos):
        super().__init__(f"{message} at position {pos}")
        self.pos = pos


class ParserError(InterpreterError):
    def __init__(self, message, token):
        super().__init__(f"{message}, got {token}")
        self.token = token


class EvalError(InterpreterError):
    pass
