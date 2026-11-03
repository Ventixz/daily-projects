# Learning: Genetic Algorithms (Python)

**Source:** "Learn Python For Data Science by Doing Several Projects → Part 6: Genetic Algorithms" in the Python / Data Science
section of [practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Scope: a stdlib-only GA that evolves a random string toward a target string (the tutorial is a video; this is my own implementation of the idea).

Run: `python3 ga.py "hello genetic world"`. Test: `python3 -m unittest -v`.

## Layout

- `ga.py` — `fitness`, `random_individual`, `tournament` selection, single-point `crossover`, `mutate`, and the `evolve` loop (with elitism).
- `test_ga.py` — 8 tests: fitness, validity, crossover, mutation extremes, selection pressure, convergence, monotonic best fitness, determinism.

## What I learned

- **A GA is just: score, select, recombine, mutate, repeat.** The fitness function is the only problem-specific part.
- **Tournament selection** (best of k random picks) gives tunable selection pressure without sorting or normalising scores.
- **Elitism makes best fitness monotonic.** Copying the top individuals unchanged guarantees progress never regresses, which is also easy to test.
- **Mutation rate is a balance:** too low and the population stalls on a local optimum, too high and it behaves like random search. About 2% worked here.
- **Seeding one `random.Random` makes stochastic code testable**, so convergence and determinism can be asserted.
- It reached the 19-character target in 27 generations with a population of 200 (seed 0).

## Known limits

- Toy fitness (character matches); real problems need a domain-specific fitness and encoding.
- Only single-point crossover and uniform mutation; no adaptive rates or diversity tracking.
