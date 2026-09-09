import unittest

from src.lexer import tokenize


def kinds(text):
    return [t.kind for t in tokenize(text)]


class TestTokenize(unittest.TestCase):
    def test_number(self):
        self.assertEqual(kinds("42"), ["NUM", "EOF"])

    def test_all_operators_and_parens(self):
        self.assertEqual(
            kinds("(1 + 2 - 3 * 4 / 5)"),
            ["LPAREN", "NUM", "PLUS", "NUM", "MINUS", "NUM",
             "STAR", "NUM", "SLASH", "NUM", "RPAREN", "EOF"],
        )

    def test_variable(self):
        tokens = tokenize("x")
        self.assertEqual(tokens[0].kind, "VAR")
        self.assertEqual(tokens[0].value, "x")

    def test_whitespace_is_ignored(self):
        self.assertEqual(kinds("1   +\t2\n"), ["NUM", "PLUS", "NUM", "EOF"])

    def test_multi_digit_number_value(self):
        self.assertEqual(tokenize("12345")[0].value, 12345)

    def test_multi_letter_identifier_rejected(self):
        with self.assertRaises(SyntaxError):
            tokenize("ab")

    def test_uppercase_variable_rejected(self):
        with self.assertRaises(SyntaxError):
            tokenize("X")

    def test_unknown_character_rejected(self):
        with self.assertRaises(SyntaxError):
            tokenize("1 % 2")


if __name__ == "__main__":
    unittest.main()
