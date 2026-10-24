"""K-nearest-neighbours song recommender, standard library only."""
import math
import sys

# name: (tempo bpm, energy 0-1, acousticness 0-1, danceability 0-1)
SONGS = {
    "Midnight Drive": (118, 0.80, 0.10, 0.75),
    "Neon Lights": (124, 0.88, 0.05, 0.85),
    "Club Pulse": (128, 0.92, 0.03, 0.90),
    "Sunday Porch": (82, 0.25, 0.90, 0.40),
    "Campfire Song": (90, 0.30, 0.95, 0.35),
    "Quiet Rain": (70, 0.15, 0.85, 0.25),
    "Highway Rock": (140, 0.95, 0.10, 0.50),
    "Garage Riot": (150, 0.98, 0.05, 0.45),
    "Lazy River": (95, 0.40, 0.70, 0.55),
    "Coffee Shop": (100, 0.45, 0.65, 0.60),
}


def normalise(songs):
    """Min-max scale each feature to [0, 1] so tempo doesn't dominate distance."""
    cols = list(zip(*songs.values()))
    lo = [min(c) for c in cols]
    hi = [max(c) for c in cols]
    return {
        name: tuple(0.0 if h == l else (v - l) / (h - l) for v, l, h in zip(vec, lo, hi))
        for name, vec in songs.items()
    }


def euclidean(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def recommend(name, k=3, songs=SONGS):
    """Return the k nearest other songs to `name` as [(song, distance)]."""
    if name not in songs:
        raise KeyError(name)
    if k < 1:
        raise ValueError("k must be >= 1")
    vecs = normalise(songs)
    dists = [(n, euclidean(vecs[name], v)) for n, v in vecs.items() if n != name]
    dists.sort(key=lambda t: (t[1], t[0]))
    return dists[:k]


def main(argv):
    if not argv:
        print("usage: recommender.py SONG [K]\nsongs: " + ", ".join(SONGS))
        return 1
    k = int(argv[1]) if len(argv) > 1 else 3
    try:
        for n, d in recommend(argv[0], k):
            print(f"{n}  (distance {d:.3f})")
    except KeyError:
        print(f"unknown song: {argv[0]}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
