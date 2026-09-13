//! The bridge between the simulation and the genetic algorithm: an
//! [`Animal`] knows nothing about chromosomes, and [`GeneticAlgorithm`] knows
//! nothing about eyes or brains. `AnimalIndividual` is the small adapter
//! that lets one evolve the other.

use crate::animal::Animal;
use crate::genetic_algorithm::{Chromosome, Individual};
use rand::RngCore;

pub struct AnimalIndividual {
    fitness: f32,
    chromosome: Chromosome,
}

impl AnimalIndividual {
    pub fn from_animal(animal: &Animal) -> Self {
        Self {
            fitness: animal.satiation() as f32,
            chromosome: animal.as_chromosome(),
        }
    }

    pub fn into_animal(self, rng: &mut dyn RngCore) -> Animal {
        Animal::from_chromosome(self.chromosome, rng)
    }
}

impl Individual for AnimalIndividual {
    fn fitness(&self) -> f32 {
        self.fitness
    }

    fn chromosome(&self) -> &Chromosome {
        &self.chromosome
    }

    fn create(chromosome: Chromosome) -> Self {
        // Fitness is unknown until this individual has lived a generation —
        // 0.0 is a placeholder, never read before then.
        Self {
            fitness: 0.0,
            chromosome,
        }
    }
}
