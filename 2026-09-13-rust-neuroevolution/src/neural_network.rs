//! A tiny feed-forward network with ReLU activations. Weights round-trip
//! through a flat `Vec<f32>` (bias, then input weights, per neuron, per
//! layer, in order) so the genetic algorithm can treat a whole brain as one
//! chromosome without knowing anything about neurons or layers.

use rand::{Rng, RngCore};

#[derive(Debug, Clone, Copy)]
pub struct LayerTopology {
    pub neurons: usize,
}

#[derive(Debug)]
pub struct Network {
    layers: Vec<Layer>,
}

impl Network {
    pub fn random(rng: &mut dyn RngCore, layers: &[LayerTopology]) -> Self {
        assert!(
            layers.len() > 1,
            "a network needs at least an input and an output layer"
        );

        let layers = layers
            .windows(2)
            .map(|pair| Layer::random(rng, pair[0].neurons, pair[1].neurons))
            .collect();

        Self { layers }
    }

    /// Rebuilds a network from a flat weight stream — the inverse of
    /// [`Network::weights`]. Panics if the stream doesn't have exactly
    /// enough values for the given topology.
    pub fn from_weights(layers: &[LayerTopology], weights: impl IntoIterator<Item = f32>) -> Self {
        assert!(
            layers.len() > 1,
            "a network needs at least an input and an output layer"
        );

        let mut weights = weights.into_iter();

        let layers = layers
            .windows(2)
            .map(|pair| Layer::from_weights(pair[0].neurons, pair[1].neurons, &mut weights))
            .collect();

        assert!(weights.next().is_none(), "got more weights than this topology needs");

        Self { layers }
    }

    pub fn weights(&self) -> impl Iterator<Item = f32> + '_ {
        self.layers
            .iter()
            .flat_map(|layer| layer.neurons.iter())
            .flat_map(|neuron| std::iter::once(neuron.bias).chain(neuron.weights.iter().copied()))
    }

    pub fn propagate(&self, inputs: Vec<f32>) -> Vec<f32> {
        self.layers.iter().fold(inputs, |inputs, layer| layer.propagate(inputs))
    }
}

#[derive(Debug)]
struct Layer {
    neurons: Vec<Neuron>,
}

impl Layer {
    fn random(rng: &mut dyn RngCore, input_size: usize, output_size: usize) -> Self {
        let neurons = (0..output_size).map(|_| Neuron::random(rng, input_size)).collect();

        Self { neurons }
    }

    fn from_weights(input_size: usize, output_size: usize, weights: &mut dyn Iterator<Item = f32>) -> Self {
        let neurons = (0..output_size)
            .map(|_| Neuron::from_weights(input_size, weights))
            .collect();

        Self { neurons }
    }

    fn propagate(&self, inputs: Vec<f32>) -> Vec<f32> {
        self.neurons.iter().map(|neuron| neuron.propagate(&inputs)).collect()
    }
}

#[derive(Debug)]
struct Neuron {
    bias: f32,
    weights: Vec<f32>,
}

impl Neuron {
    fn random(rng: &mut dyn RngCore, input_size: usize) -> Self {
        let bias = rng.gen_range(-1.0..=1.0);
        let weights = (0..input_size).map(|_| rng.gen_range(-1.0..=1.0)).collect();

        Self { bias, weights }
    }

    fn from_weights(input_size: usize, weights: &mut dyn Iterator<Item = f32>) -> Self {
        let bias = weights.next().expect("not enough weights for a neuron's bias");
        let weights = (0..input_size)
            .map(|_| weights.next().expect("not enough weights for a neuron's inputs"))
            .collect();

        Self { bias, weights }
    }

    fn propagate(&self, inputs: &[f32]) -> f32 {
        assert_eq!(inputs.len(), self.weights.len());

        let sum: f32 = inputs
            .iter()
            .zip(&self.weights)
            .map(|(input, weight)| input * weight)
            .sum();

        (self.bias + sum).max(0.0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn propagate_matches_hand_computed_relu() {
        let network = Network::from_weights(
            &[LayerTopology { neurons: 2 }, LayerTopology { neurons: 1 }],
            // one neuron: bias -0.5, weights [0.5, -1.0]
            vec![-0.5, 0.5, -1.0],
        );

        // (0.5*1.0) + (-1.0*1.0) - 0.5 = -1.0 -> ReLU clamps to 0
        assert_eq!(network.propagate(vec![1.0, 1.0]), vec![0.0]);

        // (0.5*4.0) + (-1.0*0.0) - 0.5 = 1.5 -> passes through
        assert_eq!(network.propagate(vec![4.0, 0.0]), vec![1.5]);
    }

    #[test]
    fn weights_round_trip_through_from_weights() {
        let mut rng = rand::rngs::mock::StepRng::new(2, 1);
        let topology = [
            LayerTopology { neurons: 3 },
            LayerTopology { neurons: 2 },
            LayerTopology { neurons: 1 },
        ];

        let original = Network::random(&mut rng, &topology);
        let weights: Vec<f32> = original.weights().collect();

        let rebuilt = Network::from_weights(&topology, weights.clone());
        let rebuilt_weights: Vec<f32> = rebuilt.weights().collect();

        assert_eq!(weights, rebuilt_weights);
    }

    #[test]
    #[should_panic(expected = "not enough weights")]
    fn from_weights_panics_on_too_few_weights() {
        Network::from_weights(&[LayerTopology { neurons: 2 }, LayerTopology { neurons: 1 }], vec![0.0]);
    }

    #[test]
    #[should_panic(expected = "got more weights")]
    fn from_weights_panics_on_too_many_weights() {
        Network::from_weights(
            &[LayerTopology { neurons: 2 }, LayerTopology { neurons: 1 }],
            vec![0.0, 0.0, 0.0, 99.0],
        );
    }
}
