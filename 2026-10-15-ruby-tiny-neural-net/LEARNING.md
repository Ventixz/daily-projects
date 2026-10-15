# Learning: Tiny Neural Network in Ruby

**Source:** "Neural network in 11 lines" (iamtrask, *A Neural Network in 11 Lines of Python*) from the Deep Learning
section of [practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Re-built in pure Ruby (stdlib only, no gems, no numpy), with a hidden layer and full backpropagation.

Run: `ruby neural_net.rb` (trains XOR). Test: `ruby test_neural_net.rb`.

## Layout

- `neural_net.rb` — `NeuralNet.sigmoid`, `sigmoid_deriv`, `dot`, and `Network` (`forward`, `train_step`, `train`).
- `test_neural_net.rb` — 9 minitest tests: sigmoid, derivative, matrix product, shapes, seeding, output range, error decreasing, XOR, copy-input.

## What I learned

- **Backprop is the chain rule with bookkeeping.** Output delta = error * s(1-s); hidden delta = (delta · W1ᵀ) * s(1-s).
- **Sigmoid's derivative is cheap** when written in terms of its output, so the forward pass values get reused.
- **A hidden layer is what makes XOR learnable**; a single layer can't separate it.
- **Seeded random init** makes training reproducible, so tests on "it learns" are stable instead of flaky.
- **Matrix shapes tell you the code**: if the shapes line up (`xᵀ · delta`), the gradient is usually right.

## Known limits

- **No bias terms** (as in the original tutorial). Because of that, the input `[0,0]` always yields hidden activations of 0.5.
  My first AND test failed for exactly this reason (it plateaued near 0.5), so I swapped it for a "copy first input" test.
- Full-batch gradient descent only; pure-Ruby nested arrays are slow beyond toy sizes.
- Fixed sigmoid activation and mean-absolute-error reporting; no momentum or learning-rate schedule.
