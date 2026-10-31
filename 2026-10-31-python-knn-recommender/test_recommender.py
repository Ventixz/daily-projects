import unittest

import recommender as r


class RecommenderTest(unittest.TestCase):
    def test_euclidean(self):
        self.assertAlmostEqual(r.euclidean((0, 0), (3, 4)), 5.0)

    def test_cosine_identical_and_zero(self):
        self.assertAlmostEqual(r.cosine_distance((1, 2), (2, 4)), 0.0)
        self.assertEqual(r.cosine_distance((0, 0), (1, 1)), 1.0)

    def test_normalise_range(self):
        for vec in r.SONGS.values():
            for v in r.normalise(vec):
                self.assertTrue(0.0 <= v <= 1.0)

    def test_excludes_self_and_sorted(self):
        res = r.recommend("Sunday Piano", k=5)
        self.assertEqual(len(res), 5)
        self.assertNotIn("Sunday Piano", [n for n, _ in res])
        dists = [d for _, d in res]
        self.assertEqual(dists, sorted(dists))

    def test_quiet_neighbours(self):
        names = [n for n, _ in r.recommend("Sunday Piano", k=2)]
        self.assertEqual(set(names), {"Quiet Rain", "Lullaby"})

    def test_loud_neighbours(self):
        self.assertEqual(r.recommend("Thunder Road", k=1)[0][0], "Iron Pulse")

    def test_cosine_metric_runs(self):
        res = r.recommend("Disco Fever", k=2, metric=r.cosine_distance)
        self.assertEqual(len(res), 2)

    def test_k_larger_than_catalog(self):
        self.assertEqual(len(r.recommend("Lullaby", k=100)), len(r.SONGS) - 1)

    def test_errors(self):
        with self.assertRaises(KeyError):
            r.recommend("Nope")
        with self.assertRaises(ValueError):
            r.recommend("Lullaby", k=0)

    def test_cli(self):
        self.assertEqual(r.main(["Lullaby", "2"]), 0)
        self.assertEqual(r.main(["Nope"]), 1)
        self.assertEqual(r.main([]), 2)


if __name__ == "__main__":
    unittest.main()
