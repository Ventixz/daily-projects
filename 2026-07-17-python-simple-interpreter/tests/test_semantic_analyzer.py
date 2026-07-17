import unittest

from pascal_interpreter import run_source
from pascal_interpreter.semantic_analyzer import SemanticError


class TestSemanticAnalyzer(unittest.TestCase):
    def test_undeclared_variable_on_rhs_is_rejected(self):
        source = """
        PROGRAM P;
        VAR x : INTEGER;
        BEGIN
           x := y + 1;
        END.
        """
        with self.assertRaises(SemanticError):
            run_source(source)

    def test_assignment_to_undeclared_variable_is_rejected(self):
        source = """
        PROGRAM P;
        VAR x : INTEGER;
        BEGIN
           x := 1;
           y := 2;
        END.
        """
        with self.assertRaises(SemanticError):
            run_source(source)

    def test_duplicate_declaration_is_rejected(self):
        source = """
        PROGRAM P;
        VAR
           x : INTEGER;
           x : REAL;
        BEGIN
           x := 1;
        END.
        """
        with self.assertRaises(SemanticError):
            run_source(source)

    def test_semantic_errors_are_caught_before_any_statement_runs(self):
        # If semantic analysis actually runs first, the division by zero on line 2
        # never gets a chance to raise -- the undeclared-variable error on line 3 wins.
        source = """
        PROGRAM P;
        VAR x : INTEGER;
        BEGIN
           x := 1 DIV 0;
           x := undeclared;
        END.
        """
        with self.assertRaises(SemanticError):
            run_source(source)

    def test_well_formed_program_declares_and_uses_multiple_vars(self):
        source = """
        PROGRAM P;
        VAR
           a, b : INTEGER;
           c : REAL;
        BEGIN
           a := 5;
           b := a * 2;
           c := b / 4;
        END.
        """
        scope = run_source(source)
        self.assertEqual(scope["a"], 5)
        self.assertEqual(scope["b"], 10)
        self.assertAlmostEqual(scope["c"], 2.5)


if __name__ == "__main__":
    unittest.main()
