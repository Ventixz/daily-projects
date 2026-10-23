# Learning: Neural Network from Scratch in Python

**Source:** "Build Your Own Neural Net from Scratch in Python" in the Python → Machine Learning section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Built with only the Python standard library (no NumPy).

Run: `python3 neural_net.py [epochs]` (trains on XOR and prints predictions). Test: `python3 -m unittest`.

## Layout

- `neural_net.py` — `sigmoid`, `NeuralNet` (forward pass, per-sample backprop `train_step`, `fit`), XOR demo CLI.
- `test_neural_net.py` — 8 tests: sigmoid bounds/derivative, shapes, seed determinism, a numeric gradient check,
  XOR convergence, loss decrease, CLI.

## What I learned

- **XOR needs a hidden layer.** A single sigmoid unit can only draw one line; the hidden layer bends the space so
  the two classes become separable.
- **Backprop is the chain rule bookkeeping.** Output delta = `(o - t) * o(1-o)`; each hidden delta is the output
  deltas pushed back through the weights, times the hidden unit's own `h(1-h)`.
- **Sigmoid's derivative comes free from its output**, so the forward pass values are all backprop needs.
- **Gradient checking catches silent bugs.** Comparing the analytic update to a central finite difference
  (the test) proves the backprop maths without needing the network to "look like" it works.
- **Shuffling and seeds matter.** Per-epoch shuffling avoids order effects; fixed seeds make training reproducible
  and tests non-flaky.

## Known limits

- Pure-Python loops are slow; real work needs vectorised arrays.
- Plain SGD with a fixed learning rate and sigmoid units; no momentum, ReLU, or mini-batches.
- A bad initialisation can still stall XOR in a local minimum; the tests pin a seed that converges.
