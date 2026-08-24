import unittest

from src.tokenizer import tokenize


class TestTokenizer(unittest.TestCase):
    def test_lowercases_and_strips_punctuation(self):
        self.assertEqual(
            tokenize("Hello, World! It's great.", drop_stopwords=False),
            ["hello", "world", "it's", "great"],
        )

    def test_drops_stopwords_by_default(self):
        self.assertEqual(tokenize("the cat sat on the mat"), ["cat", "sat", "mat"])

    def test_keeps_stopwords_when_disabled(self):
        self.assertEqual(
            tokenize("the cat sat", drop_stopwords=False), ["the", "cat", "sat"]
        )

    def test_empty_string(self):
        self.assertEqual(tokenize(""), [])

    def test_numbers_are_tokens(self):
        self.assertEqual(tokenize("room 42b"), ["room", "42b"])


if __name__ == "__main__":
    unittest.main()
