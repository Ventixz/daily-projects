import unittest

from src.index import InvertedIndex

DOCS = {
    "d1": "the cat sat on the mat",
    "d2": "the dog sat on the log",
    "d3": "cats and dogs are friends",
}


class TestInvertedIndex(unittest.TestCase):
    def setUp(self):
        self.index = InvertedIndex.from_documents(DOCS)

    def test_num_docs(self):
        self.assertEqual(self.index.num_docs, 3)

    def test_postings_for_shared_term(self):
        self.assertEqual(self.index.docs_containing("sat"), {"d1", "d2"})

    def test_postings_for_unique_term(self):
        self.assertEqual(self.index.docs_containing("mat"), {"d1"})

    def test_term_frequency(self):
        self.assertEqual(self.index.term_frequency("sat", "d1"), 1)
        self.assertEqual(self.index.term_frequency("sat", "d3"), 0)

    def test_stopwords_are_not_indexed(self):
        # "the" is a stopword, dropped by the tokenizer before indexing --
        # it should behave exactly like a term that was never seen.
        self.assertEqual(self.index.term_frequency("the", "d1"), 0)
        self.assertEqual(self.index.docs_containing("the"), set())

    def test_document_frequency(self):
        self.assertEqual(self.index.document_frequency("sat"), 2)
        self.assertEqual(self.index.document_frequency("nonexistent"), 0)

    def test_unknown_term_has_no_docs(self):
        self.assertEqual(self.index.docs_containing("spaceship"), set())

    def test_duplicate_doc_id_rejected(self):
        idx = InvertedIndex()
        idx.add_document("d1", "hello")
        with self.assertRaises(ValueError):
            idx.add_document("d1", "again")

    def test_doc_lengths_counts_tokens_after_stopword_removal(self):
        # "the cat sat on the mat" -> stopwords the/on dropped -> cat, sat, mat
        self.assertEqual(self.index.doc_lengths["d1"], 3)


if __name__ == "__main__":
    unittest.main()
