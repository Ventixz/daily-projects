use crate::vec2::Vec2;
use rand::RngCore;

#[derive(Debug, Clone, Copy)]
pub struct Food {
    position: Vec2,
}

impl Food {
    pub fn random(rng: &mut dyn RngCore) -> Self {
        Self {
            position: Vec2::random(rng),
        }
    }

    pub fn at(position: Vec2) -> Self {
        Self { position }
    }

    pub fn position(&self) -> Vec2 {
        self.position
    }

    pub fn set_position(&mut self, position: Vec2) {
        self.position = position;
    }
}
