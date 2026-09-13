//! Minimal 2D vector math — just enough for a torus-shaped world (positions
//! wrap at the edges) and heading-based movement. Not a general-purpose
//! library; e.g. `wrap` only makes sense for positions, not directions.

use rand::{Rng, RngCore};
use std::ops::{Add, AddAssign, Mul, Sub};

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Vec2 {
    pub x: f32,
    pub y: f32,
}

impl Vec2 {
    pub fn new(x: f32, y: f32) -> Self {
        Self { x, y }
    }

    pub fn random(rng: &mut dyn RngCore) -> Self {
        Self {
            x: rng.gen_range(0.0..1.0),
            y: rng.gen_range(0.0..1.0),
        }
    }

    pub fn from_angle(angle: f32) -> Self {
        Self {
            x: angle.cos(),
            y: angle.sin(),
        }
    }

    pub fn length(&self) -> f32 {
        (self.x * self.x + self.y * self.y).sqrt()
    }

    pub fn angle(&self) -> f32 {
        self.y.atan2(self.x)
    }

    /// Wraps both coordinates into `[min, max)`, torus-style — walking off
    /// the right edge reappears on the left.
    pub fn wrap(&mut self, min: f32, max: f32) {
        let range = max - min;
        self.x = min + (self.x - min).rem_euclid(range);
        self.y = min + (self.y - min).rem_euclid(range);
    }
}

impl Add for Vec2 {
    type Output = Vec2;

    fn add(self, rhs: Vec2) -> Vec2 {
        Vec2::new(self.x + rhs.x, self.y + rhs.y)
    }
}

impl AddAssign for Vec2 {
    fn add_assign(&mut self, rhs: Vec2) {
        self.x += rhs.x;
        self.y += rhs.y;
    }
}

impl Sub for Vec2 {
    type Output = Vec2;

    fn sub(self, rhs: Vec2) -> Vec2 {
        Vec2::new(self.x - rhs.x, self.y - rhs.y)
    }
}

impl Mul<f32> for Vec2 {
    type Output = Vec2;

    fn mul(self, rhs: f32) -> Vec2 {
        Vec2::new(self.x * rhs, self.y * rhs)
    }
}

/// Normalizes an angle (in radians) into `[-PI, PI)`, so that comparing an
/// object's heading to a bearing never has to account for the 2*PI wraparound.
pub fn wrap_angle(angle: f32) -> f32 {
    use std::f32::consts::PI;

    (angle + PI).rem_euclid(2.0 * PI) - PI
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f32::consts::PI;

    #[test]
    fn length_is_pythagorean() {
        assert!((Vec2::new(3.0, 4.0).length() - 5.0).abs() < 1e-6);
    }

    #[test]
    fn from_angle_zero_points_along_positive_x() {
        let v = Vec2::from_angle(0.0);
        assert!((v.x - 1.0).abs() < 1e-6);
        assert!(v.y.abs() < 1e-6);
    }

    #[test]
    fn wrap_brings_out_of_range_values_back_in() {
        let mut v = Vec2::new(1.2, -0.3);
        v.wrap(0.0, 1.0);

        assert!((v.x - 0.2).abs() < 1e-6);
        assert!((v.y - 0.7).abs() < 1e-6);
    }

    #[test]
    fn wrap_angle_normalizes_into_plus_minus_pi() {
        // 3*PI and -3*PI both point in the same direction ("negative x");
        // which sign of PI they land on is an implementation detail, so
        // check the magnitude rather than pin an exact endpoint.
        assert!((wrap_angle(3.0 * PI).abs() - PI).abs() < 1e-5);
        assert!((wrap_angle(-3.0 * PI).abs() - PI).abs() < 1e-5);
        assert!((wrap_angle(PI / 2.0) - PI / 2.0).abs() < 1e-6);
        assert!(wrap_angle(0.0).abs() < 1e-6);
    }
}
