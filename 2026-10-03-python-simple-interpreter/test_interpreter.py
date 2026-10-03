import unittest
from interpreter import interpret, InterpreterError


def run(body, decls="a, b : INTEGER; y : REAL;"):
    return interpret(f"PROGRAM T; VAR {decls} BEGIN {body} END.")


class InterpreterTests(unittest.TestCase):
    def test_precedence_and_parens(self):
        self.assertEqual(run("a := 2 + 3 * 4; b := (2 + 3) * 4")["A"], 14)
        self.assertEqual(run("b := (2 + 3) * 4")["B"], 20)

    def test_left_associativity(self):
        self.assertEqual(run("a := 10 - 3 - 2")["A"], 5)
        self.assertEqual(run("a := 100 DIV 5 DIV 2")["A"], 10)

    def test_unary_chain(self):
        self.assertEqual(run("a := 5 - - 3")["A"], 8)
        self.assertEqual(run("a := - - +4")["A"], 4)

    def test_div_truncates_toward_zero(self):
        self.assertEqual(run("a := -7 DIV 2")["A"], -3)

    def test_float_division_and_coercion(self):
        r = run("y := 7 / 2; a := 3; b := a; y := a")
        self.assertEqual(r["Y"], 3.0)
        self.assertIsInstance(r["Y"], float)
        self.assertEqual(run("y := 7 / 2")["Y"], 3.5)

    def test_case_insensitive_and_comments(self):
        r = interpret("program t; var A : integer; begin { hi } a := 1 { x } end.")
        self.assertEqual(r["A"], 1)

    def test_nested_blocks_and_empty_statements(self):
        r = run("BEGIN a := 1; BEGIN b := a + 1 END; END")
        self.assertEqual((r["A"], r["B"]), (1, 2))

    def test_reference_program(self):
        with open("example.pas") as f:
            r = interpret(f.read())
        self.assertEqual((r["A"], r["B"], r["C"], r["X"]), (2, 25, 27, 11))
        self.assertAlmostEqual(r["Y"], 20 / 7 + 3.14)

    def test_errors(self):
        for body in ("a := q", "z := 1", "a := 1 / 0", "a := 1 DIV 0",
                     "a := 1.5", "a := 1 +", "a := 1 $ 2"):
            with self.assertRaises(InterpreterError, msg=body):
                run(body)
        with self.assertRaises(InterpreterError):
            interpret("PROGRAM T; VAR a: INTEGER; a: REAL; BEGIN END.")
        with self.assertRaises(InterpreterError):
            interpret("PROGRAM T; BEGIN END")


if __name__ == "__main__":
    unittest.main()
