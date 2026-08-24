import os
import unittest

from src.index import InvertedIndex
from src.query import boolean_search
from src.ranking import rank

CORPUS_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "corpus")


class TestEndToEnd(unittest.TestCase):
    def setUp(self):
        self.index = InvertedIndex.from_directory(CORPUS_DIR)

    def test_loads_every_corpus_file(self):
        files = {f for f in os.listdir(CORPUS_DIR) if f.endswith(".txt")}
        self.assertEqual(set(self.index.doc_ids), files)

    def test_boolean_and_finds_python_and_programming(self):
        results = boolean_search(self.index, "python AND programming")
        self.assertIn("python_language.txt", results)
        self.assertNotIn("coffee_brewing.txt", results)
        self.assertNotIn("marathon_training.txt", results)

    def test_ranked_search_prefers_most_relevant_document(self):
        results = rank(self.index, "python programming language")
        self.assertEqual(results[0][0], "python_language.txt")

    def test_ranked_search_disambiguates_python_the_language_from_snakes(self):
        # "python" alone is ambiguous between the language and the animal;
        # adding "syntax" should pull the language doc to the top even
        # though both documents contain the word "python".
        results = dict(rank(self.index, "python syntax"))
        self.assertGreater(
            results.get("python_language.txt", 0), results.get("snake_animal.txt", 0)
        )

    def test_ranked_search_finds_ocean_topic(self):
        results = rank(self.index, "deep sea creatures")
        self.assertEqual(results[0][0], "ocean_life.txt")

    def test_unrelated_query_returns_no_results(self):
        results = rank(self.index, "cryptocurrency blockchain mining")
        self.assertEqual(results, [])


if __name__ == "__main__":
    unittest.main()
