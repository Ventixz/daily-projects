import unittest

from src.parser import parse
from src.interpreter import evaluate, truncating_div


def ev(text, variables=None):
    return evaluate(parse(text), variables or {})


class TestInterpreter(unittest.TestCase):
    def test_arithmetic(self):
        self.assertEqual(ev("1 + 2 * 3"), 7)
        self.assertEqual(ev("(1 + 2) * 3"), 9)
        self.assertEqual(ev("10 - 3 - 2"), 5)

    def test_variables(self):
        self.assertEqual(ev("a + b * c", {"a": 1, "b": 2, "c": 3}), 7)

    def test_missing_variable_defaults_to_zero(self):
        self.assertEqual(ev("a + 1", {}), 1)

    def test_unary_minus(self):
        self.assertEqual(ev("-5 + 3"), -2)

    def test_truncating_div_matches_c_semantics(self):
        cases = [
            (7, 2, 3),
            (-7, 2, -3),
            (7, -2, -3),
            (-7, -2, 3),
            (6, 3, 2),
            (0, 5, 0),
        ]
        for a, b, expected in cases:
            with self.subTest(a=a, b=b):
                self.assertEqual(truncating_div(a, b), expected)

    def test_division_by_zero_raises(self):
        with self.assertRaises(ZeroDivisionError):
            ev("1 / 0")


if __name__ == "__main__":
    unittest.main()
