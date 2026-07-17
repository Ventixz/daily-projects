import unittest

from pascal_interpreter.lexer import Lexer, LexerError


class TestLexer(unittest.TestCase):
    def token_types(self, text):
        return [t.type for t in Lexer(text).tokenize()]

    def test_recognizes_keywords_case_insensitively(self):
        types = self.token_types("program var begin end")
        self.assertEqual(types, ["PROGRAM", "VAR", "BEGIN", "END", "EOF"])

    def test_integer_and_real_constants(self):
        tokens = Lexer("42 3.14").tokenize()
        self.assertEqual((tokens[0].type, tokens[0].value), ("INTEGER_CONST", 42))
        self.assertEqual((tokens[1].type, tokens[1].value), ("REAL_CONST", 3.14))

    def test_assign_is_a_single_token_not_colon_then_equals(self):
        tokens = Lexer("x := 1").tokenize()
        types = [t.type for t in tokens]
        self.assertEqual(types, ["ID", "ASSIGN", "INTEGER_CONST", "EOF"])

    def test_curly_brace_comments_are_skipped(self):
        types = self.token_types("x {this is a comment} := 1")
        self.assertEqual(types, ["ID", "ASSIGN", "INTEGER_CONST", "EOF"])

    def test_div_keyword_vs_float_div_operator(self):
        types = self.token_types("a DIV b / c")
        self.assertEqual(types, ["ID", "INTEGER_DIV", "ID", "FLOAT_DIV", "ID", "EOF"])

    def test_unterminated_comment_raises(self):
        with self.assertRaises(LexerError):
            Lexer("{ never closed").tokenize()

    def test_unexpected_character_raises(self):
        with self.assertRaises(LexerError):
            Lexer("x := 1 @ 2").tokenize()

    def test_tracks_line_and_column(self):
        tokens = Lexer("x\n  y").tokenize()
        x_token, y_token = tokens[0], tokens[1]
        self.assertEqual(x_token.line, 1)
        self.assertEqual(y_token.line, 2)
        self.assertEqual(y_token.column, 3)


if __name__ == "__main__":
    unittest.main()
