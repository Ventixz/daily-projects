import random
import unittest
import ga


class GATests(unittest.TestCase):
    def test_fitness(self):
        self.assertEqual(ga.fitness("abc", "abc"), 3)
        self.assertEqual(ga.fitness("abc", "xbz"), 1)

    def test_random_individual_valid(self):
        s = ga.random_individual(30, random.Random(1))
        self.assertEqual(len(s), 30)
        self.assertTrue(set(s) <= set(ga.ALPHABET))

    def test_crossover_mixes_parents(self):
        c = ga.crossover("aaaa", "bbbb", random.Random(2))
        self.assertEqual(len(c), 4)
        self.assertTrue(c.startswith("a") and c.endswith("b"))

    def test_mutate_zero_and_full(self):
        rng = random.Random(3)
        self.assertEqual(ga.mutate("hello", rng, 0.0), "hello")
        self.assertEqual(len(ga.mutate("hello", rng, 1.0)), 5)

    def test_tournament_prefers_fit(self):
        pop = ["a", "b", "c"]
        self.assertEqual(ga.tournament(pop, [0, 0, 9], random.Random(4), k=3), "c")

    def test_converges(self):
        best, gens, hist = ga.evolve("hello genetic world", seed=7)
        self.assertEqual(best, "hello genetic world")
        self.assertLess(gens, 1000)

    def test_elitism_monotonic(self):
        _, _, hist = ga.evolve("evolution", seed=11)
        self.assertEqual(hist, sorted(hist))

    def test_deterministic(self):
        self.assertEqual(ga.evolve("repeat me", seed=5), ga.evolve("repeat me", seed=5))


if __name__ == "__main__":
    unittest.main()
