use crate::animal::Animal;
use crate::food::Food;
use rand::RngCore;

#[derive(Debug)]
pub struct World {
    animals: Vec<Animal>,
    foods: Vec<Food>,
}

impl World {
    pub fn random(rng: &mut dyn RngCore, num_animals: usize, num_foods: usize) -> Self {
        let animals = (0..num_animals).map(|_| Animal::random(rng)).collect();
        let foods = (0..num_foods).map(|_| Food::random(rng)).collect();

        Self { animals, foods }
    }

    pub fn animals(&self) -> &[Animal] {
        &self.animals
    }

    pub fn foods(&self) -> &[Food] {
        &self.foods
    }

    pub fn animals_mut(&mut self) -> &mut [Animal] {
        &mut self.animals
    }

    pub fn set_animals(&mut self, animals: Vec<Animal>) {
        self.animals = animals;
    }

    pub fn set_foods(&mut self, foods: Vec<Food>) {
        self.foods = foods;
    }

    /// A disjoint borrow: mutate every animal's brain output while reading
    /// (not mutating) where the food currently is. Two separate `&mut self`
    /// methods returning `(&mut Vec<_>, &Vec<_>)` from the same struct would
    /// fight the borrow checker; one method that touches both fields at once
    /// doesn't, because the compiler can see the fields don't overlap.
    pub fn animals_and_foods(&mut self) -> (&mut [Animal], &[Food]) {
        (&mut self.animals, &self.foods)
    }

    pub fn animals_and_foods_mut(&mut self) -> (&mut [Animal], &mut [Food]) {
        (&mut self.animals, &mut self.foods)
    }
}
