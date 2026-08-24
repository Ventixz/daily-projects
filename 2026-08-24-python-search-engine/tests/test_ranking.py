import unittest

from src.index import InvertedIndex
from src.ranking import cosine_similarity, idf, rank

DOCS = {
    "d1": "apple apple banana",
    "d2": "apple banana banana banana",
    "d3": "cherry cherry cherry",
}


class TestRanking(unittest.TestCase):
    def setUp(self):
        self.index = InvertedIndex.from_documents(DOCS)

    def test_idf_zero_for_term_absent_from_corpus(self):
        self.assertEqual(idf(self.index, "durian"), 0.0)

    def test_idf_higher_for_rarer_term(self):
        # "cherry" appears in 1/3 docs, "apple" in 2/3 -> cherry is rarer
        self.assertGreater(idf(self.index, "cherry"), idf(self.index, "apple"))

    def test_single_term_query_matching_pure_doc_is_perfect_cosine(self):
        # d3 is entirely "cherry" -- its vector points the same direction
        # as a "cherry"-only query vector, so cosine similarity is exactly 1.
        results = rank(self.index, "cherry")
        self.assertEqual(results[0][0], "d3")
        self.assertAlmostEqual(results[0][1], 1.0, places=9)

    def test_doc_with_no_shared_terms_is_excluded(self):
        results = rank(self.index, "banana")
        doc_ids = [doc_id for doc_id, _ in results]
        self.assertNotIn("d3", doc_ids)

    def test_doc_dominated_by_query_term_ranks_above_doc_with_mixed_terms(self):
        # d2 is 3/4 banana by count; d1 is 1/3 banana by count -- banana
        # accounts for more of d2's direction, so it should score higher
        # for a banana-only query even though both docs contain it.
        results = dict(rank(self.index, "banana"))
        self.assertGreater(results["d2"], results["d1"])

    def test_empty_query_returns_nothing(self):
        self.assertEqual(rank(self.index, ""), [])

    def test_query_with_only_unknown_terms_returns_nothing(self):
        self.assertEqual(rank(self.index, "durian"), [])

    def test_top_k_limits_results(self):
        results = rank(self.index, "apple banana cherry", top_k=1)
        self.assertEqual(len(results), 1)

    def test_cosine_similarity_symmetry(self):
        vec_a = {"x": 1.0, "y": 2.0}
        vec_b = {"x": 3.0, "y": 4.0}
        self.assertAlmostEqual(
            cosine_similarity(vec_a, vec_b), cosine_similarity(vec_b, vec_a)
        )

    def test_cosine_similarity_empty_vector_is_zero(self):
        self.assertEqual(cosine_similarity({}, {"x": 1.0}), 0.0)


if __name__ == "__main__":
    unittest.main()
