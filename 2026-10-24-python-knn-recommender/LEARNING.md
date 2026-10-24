# Learning: KNN Song Recommender in Python

**Source:** "Music Recommender using KNN" in the Python → Machine Learning section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Built with only the Python standard library (no pandas / scikit-learn).

Run: `python3 recommender.py "Club Pulse" [k]`. Test: `python3 -m unittest`.

## Layout

- `recommender.py` — a tiny song catalogue (tempo, energy, acousticness, danceability), `normalise`,
  `euclidean`, `recommend` (k nearest neighbours), and a CLI.
- `test_recommender.py` — 7 tests: scaling range, distance maths, self-exclusion, ordering, sensible
  genre neighbours, error handling, CLI.

## What I learned

- **KNN has no training step.** "Fitting" is just storing the data; all work happens at query time by
  measuring distance to every item.
- **Feature scaling is essential.** Tempo (70–150) would swamp energy (0–1) in raw Euclidean distance, so
  min-max scaling puts every feature on equal footing.
- **Recommendation = nearest neighbours of an item**, excluding the item itself.
- **Deterministic tie-breaking** (sort by distance, then name) keeps results and tests stable.
- **Test behaviour, not just maths:** asserting that club tracks neighbour club tracks and acoustic
  tracks neighbour acoustic ones checks the recommender actually makes sense.

## Known limits

- Brute-force O(n) per query; real systems use KD-trees or approximate indexes.
- Hand-picked features and a 10-song catalogue; no user history or collaborative filtering.
- Euclidean distance weights all features equally; no tuning of `k` or feature weights.
