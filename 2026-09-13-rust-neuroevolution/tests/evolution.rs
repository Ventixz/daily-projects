//! Black-box tests against the public API only (`evolution_sim::Simulation`
//! and friends) — the same surface a binary crate using this as a library
//! would see, as opposed to the unit tests inside `src/`, which reach into
//! module internals.

use evolution_sim::simulation::GENERATION_LENGTH;
use evolution_sim::Simulation;
use rand::rngs::StdRng;
use rand::SeedableRng;

#[test]
fn population_and_food_counts_are_stable_across_many_generations() {
    let mut rng = StdRng::seed_from_u64(123);
    let mut sim = Simulation::random(&mut rng, 20, 15);

    for _ in 0..5 {
        sim.train(&mut rng);

        assert_eq!(sim.world().animals().len(), 20);
        assert_eq!(sim.world().foods().len(), 15);
    }
}

#[test]
fn average_fitness_trends_upward_over_a_long_run() {
    // A single generation's average is noisy (small population, random
    // spawns), so this compares the mean of the first few generations
    // against the mean of the last few, over a long enough run that a real
    // upward trend can't be explained away as noise. This is the same
    // property the project's README shows off with the CLI's live table:
    // brains that survive selection get measurably better at finding food.
    let mut rng = StdRng::seed_from_u64(99);
    let mut sim = Simulation::random(&mut rng, 30, 30);

    let mut early = Vec::new();
    let mut late = Vec::new();

    for generation in 0..40 {
        let stats = sim.train(&mut rng);

        if generation < 5 {
            early.push(stats.avg_fitness);
        } else if generation >= 35 {
            late.push(stats.avg_fitness);
        }
    }

    let mean = |xs: &[f32]| xs.iter().sum::<f32>() / xs.len() as f32;

    assert!(
        mean(&late) > mean(&early),
        "expected late-run average fitness ({:.2}) to beat early-run average ({:.2})",
        mean(&late),
        mean(&early)
    );
}

#[test]
fn a_full_generation_is_exactly_generation_length_ticks() {
    let mut rng = StdRng::seed_from_u64(1);
    let mut sim = Simulation::random(&mut rng, 5, 5);

    let mut ticks = 0;
    loop {
        ticks += 1;
        if sim.step(&mut rng).is_some() {
            break;
        }
    }

    assert_eq!(ticks, GENERATION_LENGTH);
}
