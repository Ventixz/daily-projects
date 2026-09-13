use crate::animal_individual::AnimalIndividual;
use crate::food::Food;
use crate::genetic_algorithm::{
    GaussianMutation, GeneticAlgorithm, RouletteWheelSelection, Statistics, UniformCrossover,
};
use crate::vec2::Vec2;
use crate::world::World;
use rand::RngCore;

/// Steps per generation before the genetic algorithm takes over. High
/// enough that a network has time to actually demonstrate whether it can
/// find food; low enough that a full run finishes in a reasonable time.
pub const GENERATION_LENGTH: usize = 2500;

/// Food and an animal "touch" once their centers are within this distance —
/// small enough that eating requires roughly aiming at the food, not just
/// passing through its neighborhood.
const FOOD_SIZE: f32 = 0.01;

pub struct Simulation {
    world: World,
    ga: GeneticAlgorithm<RouletteWheelSelection>,
    age: usize,
}

impl Simulation {
    pub fn random(rng: &mut dyn RngCore, num_animals: usize, num_foods: usize) -> Self {
        let world = World::random(rng, num_animals, num_foods);
        let ga = GeneticAlgorithm::new(
            RouletteWheelSelection,
            UniformCrossover,
            GaussianMutation::new(0.01, 0.3),
        );

        Self { world, ga, age: 0 }
    }

    pub fn world(&self) -> &World {
        &self.world
    }

    /// Advances the simulation by one tick. Returns the outgoing
    /// generation's statistics on the tick where evolution happens, `None`
    /// otherwise — most calls return `None`.
    pub fn step(&mut self, rng: &mut dyn RngCore) -> Option<Statistics> {
        self.process_brains();
        self.process_movement();
        self.process_collisions(rng);

        self.age += 1;

        if self.age >= GENERATION_LENGTH {
            Some(self.evolve(rng))
        } else {
            None
        }
    }

    /// Runs [`Simulation::step`] until exactly one generation has elapsed,
    /// returning its statistics. Convenient when the caller only cares about
    /// per-generation progress, not individual ticks.
    pub fn train(&mut self, rng: &mut dyn RngCore) -> Statistics {
        loop {
            if let Some(stats) = self.step(rng) {
                return stats;
            }
        }
    }

    fn process_brains(&mut self) {
        let (animals, foods) = self.world.animals_and_foods();

        for animal in animals {
            let vision = animal.vision(foods);
            animal.think(vision);
        }
    }

    fn process_movement(&mut self) {
        for animal in self.world.animals_mut() {
            animal.walk();
        }
    }

    fn process_collisions(&mut self, rng: &mut dyn RngCore) {
        let (animals, foods) = self.world.animals_and_foods_mut();

        for animal in animals.iter_mut() {
            for food in foods.iter_mut() {
                if (animal.position() - food.position()).length() <= FOOD_SIZE {
                    animal.eat();
                    food.set_position(Vec2::random(rng));
                }
            }
        }
    }

    fn evolve(&mut self, rng: &mut dyn RngCore) -> Statistics {
        self.age = 0;

        let current_population: Vec<AnimalIndividual> =
            self.world.animals().iter().map(AnimalIndividual::from_animal).collect();

        let (evolved_population, stats) = self.ga.evolve(rng, &current_population);

        self.world.set_animals(
            evolved_population
                .into_iter()
                .map(|individual| individual.into_animal(rng))
                .collect(),
        );

        // Scatter food fresh too, so the next generation doesn't inherit
        // whatever pattern the last generation happened to eat down to.
        let num_foods = self.world.foods().len();
        self.world
            .set_foods((0..num_foods).map(|_| Food::random(rng)).collect());

        stats
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rand::rngs::StdRng;
    use rand::SeedableRng;

    #[test]
    fn population_size_survives_a_generation() {
        let mut rng = StdRng::seed_from_u64(0);
        let mut sim = Simulation::random(&mut rng, 12, 8);

        let stats = sim.train(&mut rng);

        assert_eq!(sim.world().animals().len(), 12);
        assert_eq!(sim.world().foods().len(), 8);
        assert!(stats.min_fitness.is_finite());
        assert!(stats.max_fitness.is_finite());
        assert!(stats.avg_fitness.is_finite());
        assert!(stats.min_fitness >= 0.0, "satiation can't be negative");
    }

    #[test]
    fn several_generations_all_produce_finite_stats() {
        let mut rng = StdRng::seed_from_u64(7);
        let mut sim = Simulation::random(&mut rng, 10, 10);

        for _ in 0..3 {
            let stats = sim.train(&mut rng);
            assert!(stats.avg_fitness.is_finite());
            assert!(stats.avg_fitness <= stats.max_fitness);
            assert!(stats.avg_fitness >= stats.min_fitness);
        }
    }
}
