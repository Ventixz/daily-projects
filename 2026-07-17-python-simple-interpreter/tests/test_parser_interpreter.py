import unittest

from pascal_interpreter import run_source
from pascal_interpreter.parser import ParserError


def run(program_body, decls="x, y : INTEGER;"):
    source = f"""
    PROGRAM Test;
    VAR
       {decls}
    BEGIN
       {program_body}
    END.
    """
    return run_source(source)


class TestArithmetic(unittest.TestCase):
    def test_operator_precedence(self):
        scope = run("x := 2 + 3 * 4;")
        self.assertEqual(scope["x"], 14)

    def test_parentheses_override_precedence(self):
        scope = run("x := (2 + 3) * 4;")
        self.assertEqual(scope["x"], 20)

    def test_integer_division_truncates(self):
        scope = run("x := 7 DIV 2;")
        self.assertEqual(scope["x"], 3)

    def test_float_division_is_exact(self):
        scope = run("x := 7 / 2;")
        self.assertAlmostEqual(scope["x"], 3.5)

    def test_unary_minus_and_plus(self):
        scope = run("x := -5 + +3;")
        self.assertEqual(scope["x"], -2)

    def test_left_associativity_of_subtraction(self):
        scope = run("x := 10 - 2 - 3;")  # (10 - 2) - 3, not 10 - (2 - 3)
        self.assertEqual(scope["x"], 5)


class TestVariablesAndCompoundStatements(unittest.TestCase):
    def test_variable_assignment_and_reference(self):
        scope = run("x := 10; y := x + 5;")
        self.assertEqual(scope, {"x": 10, "y": 15})

    def test_nested_compound_statements(self):
        scope = run("x := 1; BEGIN y := x + 1 END;")
        self.assertEqual(scope, {"x": 1, "y": 2})

    def test_empty_statements_between_semicolons_are_fine(self):
        scope = run("x := 1;; y := 2;")
        self.assertEqual(scope, {"x": 1, "y": 2})


class TestSyntaxErrors(unittest.TestCase):
    def test_missing_dot_at_end_is_a_parser_error(self):
        source = "PROGRAM P; BEGIN x := 1 END"
        with self.assertRaises(ParserError):
            run_source(source)

    def test_missing_semicolon_between_statements(self):
        source = """
        PROGRAM P;
        VAR x : INTEGER;
        BEGIN
           x := 1
           x := 2
        END.
        """
        with self.assertRaises(ParserError):
            run_source(source)


if __name__ == "__main__":
    unittest.main()
