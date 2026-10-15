# frozen_string_literal: true

require 'minitest/autorun'
require_relative 'neural_net'

class NeuralNetTest < Minitest::Test
  def test_sigmoid_values
    assert_in_delta 0.5, NeuralNet.sigmoid(0), 1e-12
    assert_operator NeuralNet.sigmoid(20), :>, 0.999
    assert_operator NeuralNet.sigmoid(-20), :<, 0.001
  end

  def test_sigmoid_deriv_peak
    assert_in_delta 0.25, NeuralNet.sigmoid_deriv(0.5), 1e-12
  end

  def test_dot
    assert_equal [[19, 22], [43, 50]], NeuralNet.dot([[1, 2], [3, 4]], [[5, 6], [7, 8]])
    assert_equal [[11]], NeuralNet.dot([[1, 2]], [[3], [4]])
  end

  def test_weight_shapes
    n = NeuralNet::Network.new([3, 5, 2])
    assert_equal [3, 5], [n.w0.size, n.w0[0].size]
    assert_equal [5, 2], [n.w1.size, n.w1[0].size]
  end

  def test_seed_is_deterministic
    assert_equal NeuralNet::Network.new([2, 3, 1], seed: 7).w0,
                 NeuralNet::Network.new([2, 3, 1], seed: 7).w0
  end

  def test_output_in_unit_interval
    out = NeuralNet::Network.new([2, 3, 1]).predict([[1, 0], [0, 1]])
    assert(out.flatten.all? { |v| v > 0 && v < 1 })
  end

  def test_training_reduces_error
    x = [[0, 0], [0, 1], [1, 0], [1, 1]]
    y = [[0], [1], [1], [0]]
    n = NeuralNet::Network.new([2, 4, 1])
    before = n.train_step(x, y, 0.5)
    after = n.train(x, y, epochs: 500)
    assert_operator after, :<, before
  end

  def test_learns_xor
    x = [[0, 0], [0, 1], [1, 0], [1, 1]]
    y = [[0], [1], [1], [0]]
    n = NeuralNet::Network.new([2, 4, 1])
    n.train(x, y, epochs: 10_000)
    n.predict(x).flatten.zip(y.flatten).each { |p, t| assert_in_delta t, p, 0.1 }
  end

  def test_learns_copy_first_input
    x = [[0, 0], [0, 1], [1, 0], [1, 1]]
    y = [[0], [0], [1], [1]]
    n = NeuralNet::Network.new([2, 3, 1])
    n.train(x, y, epochs: 5000)
    n.predict(x).flatten.zip(y.flatten).each { |p, t| assert_in_delta t, p, 0.15 }
  end
end
