import io
import unittest
from contextlib import redirect_stdout

import neural_net as nn


class SigmoidTests(unittest.TestCase):
    def test_midpoint_and_bounds(self):
        self.assertAlmostEqual(nn.sigmoid(0), 0.5)
        self.assertEqual(nn.sigmoid(-1000), 0.0)
        self.assertEqual(nn.sigmoid(1000), 1.0)

    def test_derivative(self):
        self.assertAlmostEqual(nn.sigmoid_prime_from_output(0.5), 0.25)


class NetTests(unittest.TestCase):
    def test_shapes(self):
        net = nn.NeuralNet(3, 5, 2)
        h, o = net.forward([0.1, 0.2, 0.3])
        self.assertEqual((len(h), len(o)), (5, 2))

    def test_seed_is_deterministic(self):
        a = nn.NeuralNet(2, 3, 1, seed=7).predict([1, 0])
        b = nn.NeuralNet(2, 3, 1, seed=7).predict([1, 0])
        self.assertEqual(a, b)

    def test_gradient_matches_numeric(self):
        net = nn.NeuralNet(2, 3, 1, seed=3)
        x, t = [0.4, 0.9], [1]

        def loss():
            return sum((o - ti) ** 2 for o, ti in zip(net.predict(x), t))

        eps = 1e-6
        w = net.w1[0][0]
        net.w1[0][0] = w + eps
        up = loss()
        net.w1[0][0] = w - eps
        down = loss()
        net.w1[0][0] = w
        numeric = (up - down) / (2 * eps)
        # train_step applies w -= lr * d(0.5*err^2)/dw, i.e. half the squared-error gradient
        lr = 1.0
        net.train_step(x, t, lr)
        analytic = (w - net.w1[0][0]) / lr
        self.assertAlmostEqual(analytic, numeric / 2, places=6)

    def test_learns_xor(self):
        net = nn.NeuralNet(2, 4, 1)
        loss = net.fit(nn.XOR, 5000)
        self.assertLess(loss, 0.01)
        for x, t in nn.XOR:
            self.assertEqual(round(net.predict(x)[0]), t[0])

    def test_loss_decreases(self):
        net = nn.NeuralNet(2, 4, 1)
        before = sum((net.predict(x)[0] - t[0]) ** 2 for x, t in nn.XOR) / 4
        after = net.fit(nn.XOR, 2000)
        self.assertLess(after, before)

    def test_cli(self):
        buf = io.StringIO()
        with redirect_stdout(buf):
            self.assertEqual(nn.main(["x", "3000"]), 0)
        self.assertIn("want 1", buf.getvalue())


if __name__ == "__main__":
    unittest.main()
