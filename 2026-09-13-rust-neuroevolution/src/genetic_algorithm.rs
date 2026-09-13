//! A generic genetic algorithm: roulette-wheel selection, uniform crossover,
//! Gaussian mutation. It knows nothing about birds or neural networks —
//! anything that can report a fitness and hand over a flat `Chromosome` can
//! be evolved with it.

use rand::seq::SliceRandom;
use rand::{Rng, RngCore};
use std::ops::Index;

pub trait Individual {
    fn fitness(&self) -> f32;
    fn chromosome(&self) -> &Chromosome;
    fn create(chromosome: Chromosome) -> Self;
}

#[derive(Debug, Clone, PartialEq)]
pub struct Chromosome {
    genes: Vec<f32>,
}

impl Chromosome {
    pub fn len(&self) -> usize {
        self.genes.len()
    }

    pub fn is_empty(&self) -> bool {
        self.genes.is_empty()
    }

    pub fn iter(&self) -> impl Iterator<Item = &f32> {
        self.genes.iter()
    }

    pub fn iter_mut(&mut self) -> impl Iterator<Item = &mut f32> {
        self.genes.iter_mut()
    }
}

impl Index<usize> for Chromosome {
    type Output = f32;

    fn index(&self, index: usize) -> &f32 {
        &self.genes[index]
    }
}

impl FromIterator<f32> for Chromosome {
    fn from_iter<T: IntoIterator<Item = f32>>(iter: T) -> Self {
        Self {
            genes: iter.into_iter().collect(),
        }
    }
}

impl IntoIterator for Chromosome {
    type Item = f32;
    type IntoIter = std::vec::IntoIter<f32>;

    fn into_iter(self) -> Self::IntoIter {
        self.genes.into_iter()
    }
}

pub trait SelectionMethod {
    fn select<'a, I: Individual>(&self, rng: &mut dyn RngCore, population: &'a [I]) -> &'a I;
}

pub struct RouletteWheelSelection;

impl SelectionMethod for RouletteWheelSelection {
    fn select<'a, I: Individual>(&self, rng: &mut dyn RngCore, population: &'a [I]) -> &'a I {
        // A weight of exactly zero would make `choose_weighted` reject an
        // all-zero-fitness population outright (every weight zero => no
        // valid pick), which happens for real in generation zero before
        // anyone has eaten anything. A tiny floor keeps that population
        // selectable (uniformly, since all weights are then equal) instead
        // of panicking.
        population
            .choose_weighted(rng, |individual| individual.fitness().max(0.00001))
            .expect("selecting from an empty population")
    }
}

pub trait CrossoverMethod {
    fn crossover(&self, rng: &mut dyn RngCore, parent_a: &Chromosome, parent_b: &Chromosome) -> Chromosome;
}

pub struct UniformCrossover;

impl CrossoverMethod for UniformCrossover {
    fn crossover(&self, rng: &mut dyn RngCore, parent_a: &Chromosome, parent_b: &Chromosome) -> Chromosome {
        assert_eq!(parent_a.len(), parent_b.len());

        parent_a
            .iter()
            .zip(parent_b.iter())
            .map(|(&a, &b)| if rng.gen_bool(0.5) { a } else { b })
            .collect()
    }
}

pub trait MutationMethod {
    fn mutate(&self, rng: &mut dyn RngCore, child: &mut Chromosome);
}

pub struct GaussianMutation {
    /// Probability, per gene, that it gets perturbed at all.
    chance: f32,
    /// Maximum magnitude of a perturbation.
    coeff: f32,
}

impl GaussianMutation {
    pub fn new(chance: f32, coeff: f32) -> Self {
        assert!((0.0..=1.0).contains(&chance));

        Self { chance, coeff }
    }
}

impl MutationMethod for GaussianMutation {
    fn mutate(&self, rng: &mut dyn RngCore, child: &mut Chromosome) {
        for gene in child.iter_mut() {
            if rng.gen_bool(self.chance as f64) {
                let sign = if rng.gen_bool(0.5) { -1.0 } else { 1.0 };
                *gene += sign * self.coeff * rng.gen::<f32>();
            }
        }
    }
}

pub struct GeneticAlgorithm<S> {
    selection_method: S,
    crossover_method: Box<dyn CrossoverMethod>,
    mutation_method: Box<dyn MutationMethod>,
}

impl<S: SelectionMethod> GeneticAlgorithm<S> {
    pub fn new(
        selection_method: S,
        crossover_method: impl CrossoverMethod + 'static,
        mutation_method: impl MutationMethod + 'static,
    ) -> Self {
        Self {
            selection_method,
            crossover_method: Box::new(crossover_method),
            mutation_method: Box::new(mutation_method),
        }
    }

    /// Produces the next generation and the fitness statistics of the one
    /// that just ended (not the new, unevaluated one — a fresh individual's
    /// `fitness()` is meaningless until it's lived a generation).
    pub fn evolve<I: Individual>(&self, rng: &mut dyn RngCore, population: &[I]) -> (Vec<I>, Statistics) {
        assert!(!population.is_empty(), "cannot evolve an empty population");

        let new_population = (0..population.len())
            .map(|_| {
                let parent_a = self.selection_method.select(rng, population).chromosome();
                let parent_b = self.selection_method.select(rng, population).chromosome();

                let mut child = self.crossover_method.crossover(rng, parent_a, parent_b);
                self.mutation_method.mutate(rng, &mut child);

                I::create(child)
            })
            .collect();

        (new_population, Statistics::new(population))
    }
}

#[derive(Debug, Clone, Copy)]
pub struct Statistics {
    pub min_fitness: f32,
    pub max_fitness: f32,
    pub avg_fitness: f32,
}

impl Statistics {
    fn new<I: Individual>(population: &[I]) -> Self {
        assert!(!population.is_empty());

        let mut min_fitness = f32::INFINITY;
        let mut max_fitness = f32::NEG_INFINITY;
        let mut total_fitness = 0.0;

        for individual in population {
            let fitness = individual.fitness();
            min_fitness = min_fitness.min(fitness);
            max_fitness = max_fitness.max(fitness);
            total_fitness += fitness;
        }

        Self {
            min_fitness,
            max_fitness,
            avg_fitness: total_fitness / population.len() as f32,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rand::rngs::StdRng;
    use rand::SeedableRng;

    // The chromosome carries the fitness itself, gene-for-gene — the
    // standard trick for testing a GA without dragging in a real problem
    // domain. Fitness is just "sum of genes", so crossover and mutation
    // exercise the exact same code path they'd use on a real chromosome.
    #[derive(Clone, Debug, PartialEq)]
    struct TestIndividual {
        chromosome: Chromosome,
    }

    impl TestIndividual {
        fn new(genes: Vec<f32>) -> Self {
            Self {
                chromosome: genes.into_iter().collect(),
            }
        }
    }

    impl Individual for TestIndividual {
        fn fitness(&self) -> f32 {
            self.chromosome.iter().sum()
        }

        fn chromosome(&self) -> &Chromosome {
            &self.chromosome
        }

        fn create(chromosome: Chromosome) -> Self {
            Self { chromosome }
        }
    }

    #[test]
    fn statistics_reports_min_max_avg() {
        let population = vec![
            TestIndividual::new(vec![1.0]),
            TestIndividual::new(vec![2.0]),
            TestIndividual::new(vec![4.0]),
            TestIndividual::new(vec![1.0]),
        ];

        let stats = Statistics::new(&population);

        assert_eq!(stats.min_fitness, 1.0);
        assert_eq!(stats.max_fitness, 4.0);
        assert_eq!(stats.avg_fitness, 2.0);
    }

    #[test]
    fn roulette_wheel_favors_higher_fitness() {
        let mut rng = StdRng::seed_from_u64(0);
        let population = vec![
            TestIndividual::new(vec![0.0]),
            TestIndividual::new(vec![0.0]),
            TestIndividual::new(vec![0.0]),
            TestIndividual::new(vec![30.0]),
        ];

        let mut selected_high_fitness = 0;
        for _ in 0..1000 {
            let winner = RouletteWheelSelection.select(&mut rng, &population);
            if winner.fitness() == 30.0 {
                selected_high_fitness += 1;
            }
        }

        // With weights [~0, ~0, ~0, 30] the last individual should win
        // almost every draw; 1000 trials leaves generous room for the
        // "almost every" without the test being flaky.
        assert!(
            selected_high_fitness > 950,
            "expected the dominant individual to win >950/1000 draws, got {selected_high_fitness}"
        );
    }

    #[test]
    fn uniform_crossover_only_ever_copies_a_parent_gene() {
        let mut rng = StdRng::seed_from_u64(1);
        let parent_a: Chromosome = vec![1.0, 2.0, 3.0, 4.0].into_iter().collect();
        let parent_b: Chromosome = vec![10.0, 20.0, 30.0, 40.0].into_iter().collect();

        let child = UniformCrossover.crossover(&mut rng, &parent_a, &parent_b);

        assert_eq!(child.len(), 4);
        for i in 0..4 {
            assert!(child[i] == parent_a[i] || child[i] == parent_b[i]);
        }
    }

    #[test]
    fn gaussian_mutation_with_zero_chance_changes_nothing() {
        let mut rng = StdRng::seed_from_u64(2);
        let original: Chromosome = vec![1.0, 2.0, 3.0].into_iter().collect();
        let mut mutated = original.clone();

        GaussianMutation::new(0.0, 5.0).mutate(&mut rng, &mut mutated);

        assert_eq!(original, mutated);
    }

    #[test]
    fn gaussian_mutation_with_zero_coeff_changes_nothing() {
        let mut rng = StdRng::seed_from_u64(3);
        let original: Chromosome = vec![1.0, 2.0, 3.0].into_iter().collect();
        let mut mutated = original.clone();

        GaussianMutation::new(1.0, 0.0).mutate(&mut rng, &mut mutated);

        assert_eq!(original, mutated);
    }

    #[test]
    fn gaussian_mutation_with_full_chance_changes_every_gene() {
        let mut rng = StdRng::seed_from_u64(4);
        let original: Chromosome = vec![1.0, 2.0, 3.0].into_iter().collect();
        let mut mutated = original.clone();

        GaussianMutation::new(1.0, 1.0).mutate(&mut rng, &mut mutated);

        for i in 0..3 {
            assert_ne!(original[i], mutated[i]);
        }
    }

    #[test]
    fn evolving_a_sum_maximization_task_improves_average_fitness() {
        // Not a real problem (fitness = sum of genes, so the optimum is
        // "every gene as large as possible"), but it's a fast, deterministic
        // way to check the whole pipeline — selection, crossover, mutation —
        // actually composes into something that climbs, not just that each
        // piece works in isolation.
        let mut rng = StdRng::seed_from_u64(42);
        let ga = GeneticAlgorithm::new(
            RouletteWheelSelection,
            UniformCrossover,
            GaussianMutation::new(0.2, 0.3),
        );

        let mut population: Vec<TestIndividual> = (0..30)
            .map(|_| TestIndividual::new((0..8).map(|_| rng.gen_range(0.0..1.0)).collect()))
            .collect();

        let avg = |pop: &[TestIndividual]| pop.iter().map(Individual::fitness).sum::<f32>() / pop.len() as f32;
        let starting_avg = avg(&population);

        for _ in 0..30 {
            let (next, _stats) = ga.evolve(&mut rng, &population);
            population = next;
        }

        let ending_avg = avg(&population);

        assert!(
            ending_avg > starting_avg,
            "expected evolution to raise average fitness: {starting_avg} -> {ending_avg}"
        );
    }
}
