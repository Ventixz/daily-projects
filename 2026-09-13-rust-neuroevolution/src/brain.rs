//! The neural network wrapper that turns "what the eye sees" into "how the
//! muscles should respond": one hidden layer, and a fixed input/output shape
//! derived from the eye's cell count so a brain and its eye can never drift
//! out of sync with each other.

use crate::eye::Eye;
use crate::genetic_algorithm::Chromosome;
use crate::neural_network::{LayerTopology, Network};
use rand::RngCore;

const SPEED_ACCEL: f32 = 0.2;
const ROTATION_ACCEL: f32 = std::f32::consts::FRAC_PI_2;

#[derive(Debug)]
pub struct Brain {
    nn: Network,
}

impl Brain {
    pub fn random(rng: &mut dyn RngCore, eye: &Eye) -> Self {
        Self {
            nn: Network::random(rng, &Self::topology(eye)),
        }
    }

    pub fn from_chromosome(chromosome: Chromosome, eye: &Eye) -> Self {
        Self {
            nn: Network::from_weights(&Self::topology(eye), chromosome),
        }
    }

    pub fn as_chromosome(&self) -> Chromosome {
        self.nn.weights().collect()
    }

    /// Returns `(speed_delta, rotation_delta)`, each already scaled to the
    /// animal's actual physical limits. The network's raw output is an
    /// unbounded, non-negative ReLU activation; centering it at 0.5 before
    /// clamping to `[0, 1]` is what lets a "low" output mean *slow down /
    /// turn left* instead of only ever meaning "do less of a one-directional
    /// thing".
    pub fn propagate(&self, vision: Vec<f32>) -> (f32, f32) {
        let response = self.nn.propagate(vision);

        let speed_delta = (response[0].clamp(0.0, 1.0) - 0.5) * 2.0 * SPEED_ACCEL;
        let rotation_delta = (response[1].clamp(0.0, 1.0) - 0.5) * 2.0 * ROTATION_ACCEL;

        (speed_delta, rotation_delta)
    }

    fn topology(eye: &Eye) -> [LayerTopology; 3] {
        [
            LayerTopology { neurons: eye.cells() },
            LayerTopology {
                neurons: 2 * eye.cells(),
            },
            LayerTopology { neurons: 2 },
        ]
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rand::rngs::StdRng;
    use rand::SeedableRng;

    #[test]
    fn chromosome_round_trips_through_a_brain() {
        let eye = Eye::default();
        let mut rng = StdRng::seed_from_u64(0);

        let original = Brain::random(&mut rng, &eye);
        let chromosome = original.as_chromosome();

        let rebuilt = Brain::from_chromosome(chromosome.clone(), &eye);
        assert_eq!(chromosome, rebuilt.as_chromosome());
    }

    #[test]
    fn propagate_output_is_within_physical_limits() {
        let eye = Eye::default();
        let mut rng = StdRng::seed_from_u64(1);
        let brain = Brain::random(&mut rng, &eye);

        let (speed_delta, rotation_delta) = brain.propagate(vec![0.5; eye.cells()]);

        assert!((-SPEED_ACCEL..=SPEED_ACCEL).contains(&speed_delta));
        assert!((-ROTATION_ACCEL..=ROTATION_ACCEL).contains(&rotation_delta));
    }
}
