import unittest

from src.index import InvertedIndex
from src.query import BooleanQueryError, boolean_search

DOCS = {
    "d1": "python is a great programming language",
    "d2": "java is also a programming language",
    "d3": "python snakes live in trees",
    "d4": "coffee and tea are beverages",
}


class TestBooleanSearch(unittest.TestCase):
    def setUp(self):
        self.index = InvertedIndex.from_documents(DOCS)

    def test_implicit_and_single_term(self):
        self.assertEqual(boolean_search(self.index, "python"), {"d1", "d3"})

    def test_implicit_and_multiple_terms(self):
        # bare "python programming" behaves like "python AND programming"
        self.assertEqual(boolean_search(self.index, "python programming"), {"d1"})

    def test_explicit_and(self):
        self.assertEqual(
            boolean_search(self.index, "python AND programming"), {"d1"}
        )

    def test_or(self):
        self.assertEqual(
            boolean_search(self.index, "coffee OR snakes"), {"d3", "d4"}
        )

    def test_not(self):
        self.assertEqual(
            boolean_search(self.index, "programming NOT java"), {"d1"}
        )

    def test_chained_operators(self):
        self.assertEqual(
            boolean_search(self.index, "programming OR snakes NOT java"), {"d1", "d3"}
        )

    def test_no_matches(self):
        self.assertEqual(boolean_search(self.index, "nonexistent"), set())

    def test_empty_query(self):
        self.assertEqual(boolean_search(self.index, ""), set())

    def test_cannot_start_with_not(self):
        with self.assertRaises(BooleanQueryError):
            boolean_search(self.index, "NOT python")

    def test_cannot_end_with_operator(self):
        with self.assertRaises(BooleanQueryError):
            boolean_search(self.index, "python AND")

    def test_cannot_have_two_operators_in_a_row(self):
        with self.assertRaises(BooleanQueryError):
            boolean_search(self.index, "python AND OR java")


if __name__ == "__main__":
    unittest.main()
