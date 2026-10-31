"""Song recommender using k-nearest neighbours (standard library only)."""
import math
import sys

FEATURES = ("energy", "danceability", "acousticness", "tempo", "valence")

SONGS = {
    "Midnight Drive": (0.80, 0.70, 0.10, 120, 0.60),
    "Neon Lights": (0.85, 0.80, 0.05, 124, 0.70),
    "Sunday Piano": (0.15, 0.25, 0.95, 70, 0.30),
    "Quiet Rain": (0.10, 0.20, 0.90, 65, 0.25),
    "Campfire Song": (0.30, 0.40, 0.85, 90, 0.55),
    "Thunder Road": (0.95, 0.45, 0.05, 150, 0.50),
    "Iron Pulse": (0.98, 0.40, 0.02, 160, 0.35),
    "Disco Fever": (0.88, 0.95, 0.08, 118, 0.90),
    "Morning Coffee": (0.35, 0.55, 0.60, 100, 0.75),
    "Lullaby": (0.05, 0.15, 0.97, 60, 0.40),
}

# Feature ranges used to normalise every dimension to 0..1.
_RANGES = {"tempo": (50.0, 200.0)}


def normalise(vec):
    out = []
    for name, value in zip(FEATURES, vec):
        lo, hi = _RANGES.get(name, (0.0, 1.0))
        out.append((value - lo) / (hi - lo))
    return tuple(out)


def euclidean(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def cosine_distance(a, b):
    na, nb = math.sqrt(sum(x * x for x in a)), math.sqrt(sum(y * y for y in b))
    if na == 0 or nb == 0:
        return 1.0
    return 1.0 - sum(x * y for x, y in zip(a, b)) / (na * nb)


def recommend(title, k=3, catalog=SONGS, metric=euclidean):
    """Return the k nearest songs to `title` as [(title, distance)], nearest first."""
    if title not in catalog:
        raise KeyError(f"unknown song: {title}")
    if k < 1:
        raise ValueError("k must be >= 1")
    target = normalise(catalog[title])
    scored = [
        (other, metric(target, normalise(vec)))
        for other, vec in catalog.items()
        if other != title
    ]
    scored.sort(key=lambda p: (p[1], p[0]))
    return scored[:k]


def main(argv):
    if not argv:
        print("usage: recommender.py <song> [k]\nsongs: " + ", ".join(SONGS))
        return 2
    k = int(argv[1]) if len(argv) > 1 else 3
    try:
        for name, dist in recommend(argv[0], k):
            print(f"{name:<16} distance={dist:.3f}")
    except KeyError as e:
        print(e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
