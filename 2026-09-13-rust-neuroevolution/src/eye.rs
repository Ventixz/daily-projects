//! An animal's eye: a forward-facing field of view sliced into angular
//! cells. Each cell reports how much "food energy" it currently sees —
//! closer food registers stronger than distant food, and anything outside
//! the range or the angle is invisible. This turns "where is the food" into
//! a fixed-size `Vec<f32>` that a neural network can take as input,
//! regardless of how many food items actually exist.

use crate::food::Food;
use crate::vec2::{wrap_angle, Vec2};
use std::f32::consts::{FRAC_PI_4, PI};

const FOV_RANGE: f32 = 0.25;
const FOV_ANGLE: f32 = PI + FRAC_PI_4;
const CELLS: usize = 9;

#[derive(Debug, Clone, Copy)]
pub struct Eye {
    fov_range: f32,
    fov_angle: f32,
    cells: usize,
}

impl Eye {
    pub fn new(fov_range: f32, fov_angle: f32, cells: usize) -> Self {
        assert!(fov_range > 0.0);
        assert!(fov_angle > 0.0);
        assert!(cells > 0);

        Self {
            fov_range,
            fov_angle,
            cells,
        }
    }

    pub fn cells(&self) -> usize {
        self.cells
    }

    /// One entry per cell, each in `[0.0, 1.0]` — 0 for "nothing seen in
    /// this slice of the view", higher for closer food. Food outside the
    /// FOV range or angle contributes nothing, from any cell.
    pub fn process_vision(&self, position: Vec2, rotation: f32, foods: &[Food]) -> Vec<f32> {
        let mut cells = vec![0.0; self.cells];

        for food in foods {
            let offset = food.position() - position;
            let distance = offset.length();

            if distance == 0.0 || distance >= self.fov_range {
                continue;
            }

            let angle = wrap_angle(offset.angle() - rotation);

            if angle < -self.fov_angle / 2.0 || angle > self.fov_angle / 2.0 {
                continue;
            }

            // Shift from "[-fov/2, fov/2] relative to heading" to "[0, fov]"
            // so it can be divided into equal-width, zero-indexed cells.
            let angle = angle + self.fov_angle / 2.0;
            let cell = (angle / self.fov_angle * self.cells as f32) as usize;
            let cell = cell.min(self.cells - 1);

            let energy = (self.fov_range - distance) / self.fov_range;
            cells[cell] += energy;
        }

        cells
    }
}

impl Default for Eye {
    fn default() -> Self {
        Self::new(FOV_RANGE, FOV_ANGLE, CELLS)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn food_at(x: f32, y: f32) -> Food {
        Food::at(Vec2::new(x, y))
    }

    #[test]
    fn food_directly_ahead_lands_in_the_middle_cell() {
        let eye = Eye::new(1.0, PI, 3);
        let foods = vec![food_at(0.6, 0.5)];

        // Looking along +x from (0.5, 0.5): the food is dead ahead.
        let vision = eye.process_vision(Vec2::new(0.5, 0.5), 0.0, &foods);

        assert_eq!(vision.len(), 3);
        assert!(vision[1] > 0.0, "expected energy in the middle cell, got {vision:?}");
        assert_eq!(vision[0], 0.0);
        assert_eq!(vision[2], 0.0);
    }

    #[test]
    fn food_behind_the_animal_is_invisible() {
        let eye = Eye::new(1.0, PI, 3);
        let foods = vec![food_at(0.4, 0.5)];

        // Facing +x, food is at -x: straight behind, outside a PI-wide FOV.
        let vision = eye.process_vision(Vec2::new(0.5, 0.5), 0.0, &foods);

        assert!(
            vision.iter().all(|&e| e == 0.0),
            "expected nothing visible, got {vision:?}"
        );
    }

    #[test]
    fn food_beyond_fov_range_is_invisible() {
        let eye = Eye::new(0.1, PI, 3);
        let foods = vec![food_at(0.6, 0.5)];

        let vision = eye.process_vision(Vec2::new(0.5, 0.5), 0.0, &foods);

        assert!(
            vision.iter().all(|&e| e == 0.0),
            "expected nothing visible beyond fov_range, got {vision:?}"
        );
    }

    #[test]
    fn closer_food_registers_more_energy_than_farther_food() {
        let eye = Eye::new(1.0, PI, 1);

        let near = eye.process_vision(Vec2::new(0.5, 0.5), 0.0, &[food_at(0.55, 0.5)]);
        let far = eye.process_vision(Vec2::new(0.5, 0.5), 0.0, &[food_at(0.9, 0.5)]);

        assert!(near[0] > far[0]);
    }
}
