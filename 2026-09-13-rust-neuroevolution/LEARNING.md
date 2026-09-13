# Evolution Simulation with Neural Networks and a Genetic Algorithm

**Source:** ["Learning to Fly" — Evolution Simulation with Neural Networks and Genetic
Algorithm](https://pwy.io/en/posts/learning-to-fly-pt1/) by Patryk Wychowaniec, one of the
Rust entries in
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Built end-to-end in one sitting, headless (no windowing crate — a container has no display
anyway, and the interesting part was never the graphics). Only external dependency: `rand`.

## What it is

Forty short-sighted "animals" wander a wraparound 2D world scattered with food. Each one
carries its own tiny neural network as a brain; nothing is hand-coded about *how* to find
food — that behavior has to emerge from a genetic algorithm selecting, over generations, for
whichever random network weights happen to eat the most. Split by concern:

- `src/vec2.rs` — 2D vector math for a torus-shaped world (positions wrap at the edges) plus
  `wrap_angle`, which turned out to be the one place a real bug showed up (see below).
- `src/neural_network.rs` — a plain feed-forward network, ReLU activations, weights that
  round-trip through a flat `Vec<f32>` so a whole brain can be treated as one chromosome.
- `src/genetic_algorithm.rs` — generic over `Individual`: roulette-wheel selection, uniform
  crossover, Gaussian mutation. Knows nothing about birds, brains, or eyes.
- `src/eye.rs` — the sensor: a forward-facing field of view sliced into 9 angular cells, each
  reporting how much "food energy" it currently sees.
- `src/brain.rs` — wraps a `Network` sized to match an `Eye`'s cell count; turns raw vision
  into a speed delta and a rotation delta.
- `src/animal.rs` / `src/animal_individual.rs` / `src/food.rs` / `src/world.rs` — the sim
  state, and the adapter (`AnimalIndividual`) that lets the GA evolve `Animal`s without either
  one knowing the other exists.
- `src/simulation.rs` — the tick loop: think, move, check collisions, and every 2,500 ticks,
  evolve.

## Run it

```bash
cd 2026-09-13-rust-neuroevolution
cargo test                                             # 28 tests
cargo run --release -- --generations 60 --seed 42
```

```
gen    min      avg      max
  1     0.00     0.52    10.00
  2     0.00     2.20    10.00
  ...
 10     4.00    20.35    46.00
  ...
 60    16.00    32.78    53.00

final snapshot ('.' = food, arrow = an animal's heading):
                                    ^
          . .   .         v                                .
. . /   .             .         /             \
^  v   .         >                          .  v
...
```

Average food eaten per generation climbs from ~0.5 to the low 30s over 60 generations, purely
from selection pressure on random initial weights — nobody wrote steering logic.

## What it actually teaches

- **A "sensor" is really just a fixed-size encoding of unbounded input.** There can be zero
  food nearby or twenty; the network still only ever sees exactly 9 numbers. `Eye::process_vision`
  is the whole trick: bucket every visible food item into the angular cell it falls in, and sum
  a distance-weighted energy per cell. That's what makes the same tiny 9-in/2-out network able to
  react to any food layout at all.

- **Genes are opaque to the algorithm that evolves them.** `GeneticAlgorithm<S>` never imports
  `Animal`, `Brain`, or `Network` — it only knows `Individual::{fitness, chromosome, create}`.
  `AnimalIndividual` is the entire adapter: `fitness` is satiation, `chromosome` is
  `Brain::as_chromosome()` (which is just `Network::weights().collect()`). Swapping the fitness
  function for a totally different problem means writing a new `Individual` impl, not touching
  the GA at all — the reused-code-in-`genetic_algorithm.rs` tests prove this: `TestIndividual`
  evolves a "maximize the sum of my genes" toy problem with the exact same `evolve()` call the
  simulation uses.

- **`rng.gen::<f32>()` alone is not "mutation," and centering matters more than range.**
  A neuron's raw output is ReLU'd — always ≥ 0, unbounded above. Feeding that straight in as a
  "rotate left/right" signal would mean the network could only ever turn one way harder or
  softer, never reverse a decision. `Brain::propagate` clamps to `[0, 1]` first, then does
  `(x - 0.5) * 2.0 * ACCEL` — the `- 0.5` is what turns "how strongly ReLU fired" into a signed
  quantity a physical steering delta can actually use. Get that step wrong and evolution can
  still technically run; it just can never discover "turn right," because that output never
  existed as a reachable value.

- **A modulo-based angle wrap has a real off-by-epsilon bug at the far edge, and it's not
  hypothetical.** The first version of `wrap_angle` computed `angle % (2*PI)` and then nudged
  the result into `(-PI, PI]` with `if wrapped > PI { -= 2*PI } else if wrapped <= -PI { += 2*PI }`.
  `wrap_angle(-3.0 * PI)` should land exactly on the boundary, but `f32`'s `%` doesn't return a
  mathematically exact `-PI` there — it comes back a few ULPs *above* `-PI` (e.g.
  `-3.14159263` vs. the `f32` constant `-3.14159274`). That value fails `wrapped <= -PI`, so the
  correction never fires, and the angle is silently left un-normalized right at the one point
  where the whole function existed to fix it. `cargo test` caught it immediately (the boundary
  is exercised on every full about-face an animal makes, so it's not even a rare case) — but a
  test that hard-coded the sign of the fixed point would have been just as brittle. The real fix
  wasn't tightening the epsilon; it was dropping the manual branch entirely for
  `(angle + PI).rem_euclid(2.0 * PI) - PI`, which routes through `rem_euclid` (guaranteed
  non-negative, no boundary special-case) instead of hoping a subtraction lands exactly on zero.

## Known limitations (by design, to keep scope to ~2-4 hours)

- No windowed/animated rendering — the CLI prints per-generation stats and one ASCII snapshot
  at the end. The original tutorial's later parts add a `wasm`/Bevy front end; this is the
  headless simulation core those parts sit on top of.
- Vision doesn't account for the world being a torus — a food item just past the wrap-around
  edge looks far away instead of close, even though walking off the edge would reach it
  instantly. A minor, deliberate approximation (real flocking sims usually make the same call
  for the same reason: torus-aware angle math is a lot more code for a rare case).
- One eye shape, one network topology, fixed generation length (2,500 ticks) — none of these
  are exposed as CLI flags beyond population/food/generation count/seed.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Credit: ["Learning to Fly"](https://pwy.io/en/posts/learning-to-fly-pt1/) by Patryk Wychowaniec.
