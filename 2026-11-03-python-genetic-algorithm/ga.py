"""Genetic algorithm from scratch (stdlib only).

Evolves a population of bit-strings / character-strings toward a target using
selection, crossover and mutation. All randomness flows through a seeded
random.Random so runs are reproducible and testable.
"""
import random
import string

ALPHABET = string.ascii_lowercase + " "


def fitness(candidate, target):
    """Number of positions matching the target (higher is better)."""
    return sum(a == b for a, b in zip(candidate, target))


def random_individual(length, rng, alphabet=ALPHABET):
    return "".join(rng.choice(alphabet) for _ in range(length))


def tournament(pop, scores, rng, k=3):
    """Pick the best of k randomly chosen individuals."""
    idx = max(rng.sample(range(len(pop)), k), key=lambda i: scores[i])
    return pop[idx]


def crossover(a, b, rng):
    """Single-point crossover."""
    if len(a) < 2:
        return a
    cut = rng.randint(1, len(a) - 1)
    return a[:cut] + b[cut:]


def mutate(ind, rng, rate, alphabet=ALPHABET):
    return "".join(rng.choice(alphabet) if rng.random() < rate else c for c in ind)


def evolve(target, pop_size=200, mutation_rate=0.02, elite=2, max_gens=1000, seed=0):
    """Return (best_individual, generations_used, history_of_best_fitness)."""
    rng = random.Random(seed)
    pop = [random_individual(len(target), rng) for _ in range(pop_size)]
    history = []
    for gen in range(max_gens + 1):
        scores = [fitness(p, target) for p in pop]
        order = sorted(range(pop_size), key=lambda i: -scores[i])
        best = pop[order[0]]
        history.append(scores[order[0]])
        if best == target:
            return best, gen, history
        nxt = [pop[i] for i in order[:elite]]  # elitism keeps the best
        while len(nxt) < pop_size:
            child = crossover(tournament(pop, scores, rng), tournament(pop, scores, rng), rng)
            nxt.append(mutate(child, rng, mutation_rate))
        pop = nxt
    return best, max_gens, history


if __name__ == "__main__":
    import sys
    target = sys.argv[1] if len(sys.argv) > 1 else "hello genetic world"
    best, gens, hist = evolve(target)
    print(f"{best!r} after {gens} generations (fitness {hist[-1]}/{len(target)})")
