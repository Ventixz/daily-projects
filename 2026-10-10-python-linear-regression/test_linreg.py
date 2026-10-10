import unittest
from linreg import *


class T(unittest.TestCase):
    def test_simple_exact(self):
        s, b = fit_simple([0, 1, 2, 3], [1, 3, 5, 7])
        self.assertAlmostEqual(s, 2); self.assertAlmostEqual(b, 1)

    def test_simple_errors(self):
        with self.assertRaises(ValueError): fit_simple([1, 1, 1], [1, 2, 3])
        with self.assertRaises(ValueError): fit_simple([1], [1])
        with self.assertRaises(ValueError): fit_simple([1, 2], [1])

    def test_simple_noisy_known_answer(self):
        s, b = fit_simple([1, 2, 3, 4, 5], [2, 4, 5, 4, 5])
        self.assertAlmostEqual(s, 0.6); self.assertAlmostEqual(b, 2.2)

    def test_solve(self):
        x = solve([[2, 1], [1, 3]], [5, 10])
        self.assertAlmostEqual(x[0], 1); self.assertAlmostEqual(x[1], 3)

    def test_solve_needs_pivot(self):
        x = solve([[0, 1], [1, 0]], [2, 3])
        self.assertEqual([round(v) for v in x], [3, 2])

    def test_singular(self):
        with self.assertRaises(ValueError): solve([[1, 2], [2, 4]], [1, 2])

    def test_normal_equation_recovers_weights(self):
        w = [4.0, 2.5, -1.5, 0.7]
        X, y = make_data(w, n=50, noise=0.0, seed=1)
        for a, b in zip(normal_equation(X, y), w): self.assertAlmostEqual(a, b, places=6)

    def test_collinear_features_rejected(self):
        X = [[i, 2 * i] for i in range(10)]
        with self.assertRaises(ValueError): normal_equation(X, list(range(10)))

    def test_gd_matches_normal_equation(self):
        X, y = make_data([1.0, 3.0, -2.0], n=100, noise=0.3, seed=3)
        Xs, means, stds = standardize(X)
        ws = gradient_descent(Xs, y, lr=0.1, epochs=800)
        ne = normal_equation(X, y)
        self.assertAlmostEqual(mse(ws, Xs, y), mse(ne, X, y), places=6)

    def test_gd_reduces_loss(self):
        X, y = make_data([0, 1], n=30, noise=0.1, seed=5)
        w0 = [0.0, 0.0]
        self.assertLess(mse(gradient_descent(X, y, lr=0.01, epochs=50), X, y), mse(w0, X, y))

    def test_r2(self):
        X, y = make_data([1.0, 2.0], n=20, seed=2)
        self.assertAlmostEqual(r_squared([1.0, 2.0], X, y), 1.0)
        self.assertLess(r_squared([0.0, 0.0], X, y), 0.1)

    def test_standardize(self):
        Xs, m, s = standardize([[1, 5], [3, 5], [5, 5]])
        self.assertAlmostEqual(mean([r[0] for r in Xs]), 0)
        self.assertEqual([r[1] for r in Xs], [0, 0, 0])  # constant column survives


if __name__ == "__main__":
    unittest.main()
