import unittest
from recommender import SONGS, normalise, euclidean, recommend, main


class Tests(unittest.TestCase):
    def test_normalise_range(self):
        for vec in normalise(SONGS).values():
            self.assertTrue(all(0.0 <= x <= 1.0 for x in vec))

    def test_euclidean(self):
        self.assertEqual(euclidean((0, 0), (3, 4)), 5.0)
        self.assertEqual(euclidean((1, 2), (1, 2)), 0.0)

    def test_excludes_self_and_k(self):
        r = recommend("Quiet Rain", 4)
        self.assertEqual(len(r), 4)
        self.assertNotIn("Quiet Rain", [n for n, _ in r])

    def test_sorted_by_distance(self):
        d = [x for _, x in recommend("Club Pulse", 9)]
        self.assertEqual(d, sorted(d))

    def test_genre_neighbours(self):
        self.assertEqual(recommend("Club Pulse", 1)[0][0], "Neon Lights")
        self.assertEqual(recommend("Campfire Song", 1)[0][0], "Sunday Porch")
        self.assertEqual(recommend("Garage Riot", 1)[0][0], "Highway Rock")

    def test_errors(self):
        with self.assertRaises(KeyError):
            recommend("nope")
        with self.assertRaises(ValueError):
            recommend("Quiet Rain", 0)

    def test_cli(self):
        self.assertEqual(main(["Quiet Rain", "2"]), 0)
        self.assertEqual(main(["nope"]), 1)
        self.assertEqual(main([]), 1)


if __name__ == "__main__":
    unittest.main()
