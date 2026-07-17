from .interpreter import Interpreter, InterpreterError
from .lexer import Lexer, LexerError
from .parser import Parser, ParserError
from .semantic_analyzer import SemanticAnalyzer, SemanticError

__all__ = [
    "Interpreter",
    "InterpreterError",
    "Lexer",
    "LexerError",
    "Parser",
    "ParserError",
    "SemanticAnalyzer",
    "SemanticError",
]


def run_source(text):
    """Lex, parse, semantically-check, and interpret Pascal-subset source.

    Returns the final global variable scope (name -> value). Raises
    LexerError/ParserError/SemanticError/InterpreterError on failure.
    """
    lexer = Lexer(text)
    parser = Parser(lexer)
    tree = parser.parse()

    analyzer = SemanticAnalyzer()
    analyzer.analyze(tree)

    interpreter = Interpreter(tree)
    return interpreter.interpret()
