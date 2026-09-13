use crate::brain::Brain;
use crate::eye::Eye;
use crate::genetic_algorithm::Chromosome;
use crate::vec2::Vec2;
use rand::{Rng, RngCore};
use std::f32::consts::PI;

pub const SPEED_MIN: f32 = 0.001;
pub const SPEED_MAX: f32 = 0.005;

#[derive(Debug)]
pub struct Animal {
    position: Vec2,
    rotation: f32,
    speed: f32,
    eye: Eye,
    brain: Brain,
    /// Food eaten this generation — doubles as this animal's fitness once
    /// the generation ends.
    satiation: usize,
}

impl Animal {
    pub fn random(rng: &mut dyn RngCore) -> Self {
        let eye = Eye::default();
        let brain = Brain::random(rng, &eye);

        Self::new(eye, brain, rng)
    }

    pub fn from_chromosome(chromosome: Chromosome, rng: &mut dyn RngCore) -> Self {
        let eye = Eye::default();
        let brain = Brain::from_chromosome(chromosome, &eye);

        Self::new(eye, brain, rng)
    }

    fn new(eye: Eye, brain: Brain, rng: &mut dyn RngCore) -> Self {
        Self {
            position: Vec2::random(rng),
            rotation: rng.gen_range(0.0..(2.0 * PI)),
            speed: SPEED_MIN,
            eye,
            brain,
            satiation: 0,
        }
    }

    pub fn position(&self) -> Vec2 {
        self.position
    }

    pub fn rotation(&self) -> f32 {
        self.rotation
    }

    pub fn satiation(&self) -> usize {
        self.satiation
    }

    pub fn speed(&self) -> f32 {
        self.speed
    }

    pub fn as_chromosome(&self) -> Chromosome {
        self.brain.as_chromosome()
    }

    pub fn vision(&self, foods: &[crate::food::Food]) -> Vec<f32> {
        self.eye.process_vision(self.position, self.rotation, foods)
    }

    pub fn think(&mut self, vision: Vec<f32>) {
        let (speed_delta, rotation_delta) = self.brain.propagate(vision);

        self.speed = (self.speed + speed_delta).clamp(SPEED_MIN, SPEED_MAX);
        self.rotation = crate::vec2::wrap_angle(self.rotation + rotation_delta);
    }

    pub fn walk(&mut self) {
        self.position += Vec2::from_angle(self.rotation) * self.speed;
        self.position.wrap(0.0, 1.0);
    }

    pub fn eat(&mut self) {
        self.satiation += 1;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rand::rngs::StdRng;
    use rand::SeedableRng;

    #[test]
    fn new_animal_starts_at_minimum_speed_and_zero_satiation() {
        let mut rng = StdRng::seed_from_u64(0);
        let animal = Animal::random(&mut rng);

        assert_eq!(animal.satiation(), 0);
        assert_eq!(animal.speed(), SPEED_MIN);
    }

    #[test]
    fn eating_increments_satiation_only() {
        let mut rng = StdRng::seed_from_u64(1);
        let mut animal = Animal::random(&mut rng);
        let position_before = animal.position();

        animal.eat();
        animal.eat();

        assert_eq!(animal.satiation(), 2);
        assert_eq!(animal.position(), position_before);
    }
}
