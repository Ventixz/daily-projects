"""A two-layer neural network from scratch, standard library only.

Follows the shape of "Build Your Own Neural Net from Scratch in Python":
sigmoid units, a hidden layer, mean squared error, hand-written backprop.
"""
import math
import random
import sys


def sigmoid(x):
    if x < -60:
        return 0.0
    if x > 60:
        return 1.0
    return 1.0 / (1.0 + math.exp(-x))


def sigmoid_prime_from_output(y):
    """Derivative of sigmoid expressed via its output y = sigmoid(x)."""
    return y * (1.0 - y)


class NeuralNet:
    def __init__(self, n_in, n_hidden, n_out, seed=1):
        rng = random.Random(seed)
        self.w1 = [[rng.uniform(-1, 1) for _ in range(n_in)] for _ in range(n_hidden)]
        self.b1 = [0.0] * n_hidden
        self.w2 = [[rng.uniform(-1, 1) for _ in range(n_hidden)] for _ in range(n_out)]
        self.b2 = [0.0] * n_out

    def forward(self, x):
        h = [sigmoid(sum(w * xi for w, xi in zip(row, x)) + b)
             for row, b in zip(self.w1, self.b1)]
        o = [sigmoid(sum(w * hi for w, hi in zip(row, h)) + b)
             for row, b in zip(self.w2, self.b2)]
        return h, o

    def predict(self, x):
        return self.forward(x)[1]

    def train_step(self, x, target, lr):
        """One SGD step on one sample; returns its squared error."""
        h, o = self.forward(x)
        d_out = [(oi - ti) * sigmoid_prime_from_output(oi) for oi, ti in zip(o, target)]
        d_hid = [sigmoid_prime_from_output(h[j]) *
                 sum(d_out[k] * self.w2[k][j] for k in range(len(d_out)))
                 for j in range(len(h))]
        for k, d in enumerate(d_out):
            for j, hj in enumerate(h):
                self.w2[k][j] -= lr * d * hj
            self.b2[k] -= lr * d
        for j, d in enumerate(d_hid):
            for i, xi in enumerate(x):
                self.w1[j][i] -= lr * d * xi
            self.b1[j] -= lr * d
        return sum((oi - ti) ** 2 for oi, ti in zip(o, target))

    def fit(self, data, epochs, lr=0.5, seed=0):
        rng = random.Random(seed)
        data = list(data)
        loss = 0.0
        for _ in range(epochs):
            rng.shuffle(data)
            loss = sum(self.train_step(x, t, lr) for x, t in data) / len(data)
        return loss


XOR = [([0, 0], [0]), ([0, 1], [1]), ([1, 0], [1]), ([1, 1], [0])]


def main(argv):
    epochs = int(argv[1]) if len(argv) > 1 else 5000
    net = NeuralNet(2, 4, 1)
    loss = net.fit(XOR, epochs)
    print("epochs=%d final loss=%.5f" % (epochs, loss))
    for x, t in XOR:
        print("%s -> %.3f (want %d)" % (x, net.predict(x)[0], t[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
