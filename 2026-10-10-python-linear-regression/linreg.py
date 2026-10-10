"""Linear regression from scratch, standard library only.

Three ways to fit a line / hyperplane:
  * fit_simple      - closed-form slope/intercept for one feature
  * gradient_descent - iterative, batch, works for any number of features
  * normal_equation - solves (X'X) w = X'y with Gaussian elimination
"""
import random


def mean(xs):
    return sum(xs) / len(xs)


def fit_simple(xs, ys):
    """Return (slope, intercept) minimising squared error."""
    if len(xs) != len(ys) or len(xs) < 2:
        raise ValueError("need at least two paired points")
    mx, my = mean(xs), mean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        raise ValueError("xs are all identical; slope is undefined")
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    slope = sxy / sxx
    return slope, my - slope * mx


def predict(weights, row):
    """weights[0] is the bias; weights[1:] pair with row."""
    return weights[0] + sum(w * v for w, v in zip(weights[1:], row))


def mse(weights, X, y):
    return mean([(predict(weights, r) - t) ** 2 for r, t in zip(X, y)])


def r_squared(weights, X, y):
    my = mean(y)
    ss_tot = sum((t - my) ** 2 for t in y)
    ss_res = sum((t - predict(weights, r)) ** 2 for r, t in zip(X, y))
    return 1.0 if ss_tot == 0 else 1 - ss_res / ss_tot


def standardize(X):
    """Return (scaled X, means, stds) so features have mean 0, std 1."""
    cols = list(zip(*X))
    means = [mean(c) for c in cols]
    stds = [(mean([(v - m) ** 2 for v in c])) ** 0.5 or 1.0 for c, m in zip(cols, means)]
    scaled = [[(v - m) / s for v, m, s in zip(r, means, stds)] for r in X]
    return scaled, means, stds


def gradient_descent(X, y, lr=0.1, epochs=1000):
    """Batch gradient descent on MSE. Returns weights (bias first)."""
    n, k = len(X), len(X[0])
    w = [0.0] * (k + 1)
    for _ in range(epochs):
        errs = [predict(w, r) - t for r, t in zip(X, y)]
        grad = [2 * sum(errs) / n]
        for j in range(k):
            grad.append(2 * sum(e * r[j] for e, r in zip(errs, X)) / n)
        w = [wi - lr * g for wi, g in zip(w, grad)]
    return w


def solve(A, b):
    """Solve A x = b by Gaussian elimination with partial pivoting."""
    n = len(A)
    M = [list(row) + [bi] for row, bi in zip(A, b)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(M[r][c]))
        if abs(M[p][c]) < 1e-12:
            raise ValueError("singular matrix (collinear features?)")
        M[c], M[p] = M[p], M[c]
        for r in range(c + 1, n):
            f = M[r][c] / M[c][c]
            for j in range(c, n + 1):
                M[r][j] -= f * M[c][j]
    x = [0.0] * n
    for i in reversed(range(n)):
        x[i] = (M[i][n] - sum(M[i][j] * x[j] for j in range(i + 1, n))) / M[i][i]
    return x


def normal_equation(X, y):
    """Exact least squares: solve (A'A) w = A'y where A = [1 | X]."""
    A = [[1.0] + list(r) for r in X]
    k = len(A[0])
    AtA = [[sum(r[i] * r[j] for r in A) for j in range(k)] for i in range(k)]
    Aty = [sum(r[i] * t for r, t in zip(A, y)) for i in range(k)]
    return solve(AtA, Aty)


def make_data(true_w, n=100, noise=0.0, seed=0):
    rng = random.Random(seed)
    X = [[rng.uniform(-5, 5) for _ in true_w[1:]] for _ in range(n)]
    y = [predict(true_w, r) + rng.gauss(0, noise) for r in X]
    return X, y


if __name__ == "__main__":
    true_w = [4.0, 2.5, -1.5]
    X, y = make_data(true_w, n=200, noise=0.5, seed=42)
    print("true weights      :", true_w)
    ne = normal_equation(X, y)
    print("normal equation   :", [round(v, 3) for v in ne], "R2 =", round(r_squared(ne, X, y), 4))
    Xs, means, stds = standardize(X)
    ws = gradient_descent(Xs, y, lr=0.1, epochs=500)
    # undo scaling to compare with the original parameterisation
    slopes = [w / s for w, s in zip(ws[1:], stds)]
    bias = ws[0] - sum(sl * m for sl, m in zip(slopes, means))
    gd = [bias] + slopes
    print("gradient descent  :", [round(v, 3) for v in gd])
