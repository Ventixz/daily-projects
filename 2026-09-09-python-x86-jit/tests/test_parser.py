import unittest

from src.parser import parse
from src.ast_nodes import Num, Var, BinOp, Neg


class TestParser(unittest.TestCase):
    def test_single_number(self):
        self.assertEqual(parse("42"), Num(42))

    def test_single_variable(self):
        self.assertEqual(parse("x"), Var("x"))

    def test_left_associative_same_precedence(self):
        # 1 - 2 - 3 must parse as (1 - 2) - 3, not 1 - (2 - 3)
        self.assertEqual(parse("1 - 2 - 3"), BinOp("-", BinOp("-", Num(1), Num(2)), Num(3)))

    def test_multiplication_binds_tighter_than_addition(self):
        self.assertEqual(parse("1 + 2 * 3"), BinOp("+", Num(1), BinOp("*", Num(2), Num(3))))

    def test_parentheses_override_precedence(self):
        self.assertEqual(parse("(1 + 2) * 3"), BinOp("*", BinOp("+", Num(1), Num(2)), Num(3)))

    def test_unary_minus_binds_tighter_than_multiplication(self):
        self.assertEqual(parse("-2 * 3"), BinOp("*", Neg(Num(2)), Num(3)))

    def test_double_unary_minus(self):
        self.assertEqual(parse("--2"), Neg(Neg(Num(2))))

    def test_mismatched_parens_raises(self):
        with self.assertRaises(SyntaxError):
            parse("(1 + 2")

    def test_trailing_garbage_raises(self):
        with self.assertRaises(SyntaxError):
            parse("1 + 2)")

    def test_empty_input_raises(self):
        with self.assertRaises(SyntaxError):
            parse("")


if __name__ == "__main__":
    unittest.main()
