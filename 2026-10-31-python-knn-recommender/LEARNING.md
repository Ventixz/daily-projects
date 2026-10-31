# Learning: Music Recommender using KNN in Python

**Source:** "Music Recommender using KNN" in the Python → Machine Learning section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Built with only the Python standard library (no scikit-learn, no real dataset: a 10-song hand-made catalog).

Run: `python3 recommender.py "Sunday Piano" 3`. Test: `python3 -m unittest`.

## Layout

- `recommender.py` — feature vectors per song, min-max `normalise`, `euclidean` and `cosine_distance`, `recommend` (k nearest), CLI.
- `test_recommender.py` — 10 tests: distance maths, normalisation bounds, self-exclusion, ordering, neighbour sanity, k edge cases, CLI.

## What I learned

- **KNN has no training step.** The "model" is the data; all the work happens at query time by ranking distances.
- **Scale matters.** Tempo (50–200) would swamp 0–1 features in a raw distance, so every dimension is min-max scaled first.
- **The metric is a modelling choice.** Euclidean cares about magnitude; cosine only about direction, so a quiet and a loud song with the same "shape" look close under cosine.
- **Deterministic tie-breaking** (distance, then title) keeps results and tests stable.
- **Sanity tests beat accuracy numbers** here: quiet acoustic songs should neighbour each other, loud rock should too.

## Known limits

- Brute force is O(n) per query; real catalogs need an index (KD-tree, ANN).
- Features are hand-picked and unweighted; no user history or collaborative filtering.
