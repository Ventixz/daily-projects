# Learning: Linear Regression From Scratch in Python

**Source:** ["Write Linear Regression From Scratch in Python"](https://www.youtube.com/watch?v=uwwWVAgJBcM) (video)
from the Machine Learning section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
I implemented three fitting methods with only the standard library (no numpy).

Run the demo with `python3 linreg.py` and the tests with `python3 -m unittest`.

## Layout

- `linreg.py` — `fit_simple` (closed form, one feature), `gradient_descent` (any number of
  features), `normal_equation` (exact, via Gaussian elimination), plus `mse`, `r_squared`,
  `standardize`, and a synthetic data generator.
- `test_linreg.py` — 12 tests, including known answers and error cases.

## What I learned

- **One feature has a closed form.** slope = Σ(x−x̄)(y−ȳ) / Σ(x−x̄)², and the line passes
  through the mean point.
- **The normal equation is a linear system.** Solving (AᵀA)w = Aᵀy with a bias column of
  ones gives the exact least-squares weights; no iteration needed.
- **Partial pivoting matters.** A test with a zero on the diagonal fails without row swaps.
- **Collinear features make AᵀA singular.** The solver raises instead of returning garbage.
- **Gradient descent needs scaled features.** Standardizing columns lets one learning rate
  (0.1) converge in a few hundred epochs; the weights must then be mapped back
  (slope/std, bias − Σ slope·mean) to compare with the unscaled solution.
- **Cross-checking methods is a good test.** Gradient descent's loss matching the normal
  equation's is a stronger check than eyeballing weights.

## Known limits

- Normal equation is O(k³) and numerically weaker than QR/SVD for ill-conditioned data.
- Gradient descent is full-batch with a fixed learning rate and epoch count; no early stopping.
- No regularization, train/test splitting, or non-linear features.
