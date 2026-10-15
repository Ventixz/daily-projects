# frozen_string_literal: true

# A tiny two-layer neural network in pure Ruby (no gems), after
# iamtrask's "A Neural Network in 11 lines of Python".
# Matrices are arrays of row arrays.
module NeuralNet
  module_function

  def sigmoid(x) = 1.0 / (1.0 + Math.exp(-x))

  # Derivative expressed in terms of the sigmoid *output* s.
  def sigmoid_deriv(s) = s * (1.0 - s)

  def dot(a, b)
    bt = b.transpose
    a.map { |row| bt.map { |col| row.zip(col).sum { |x, y| x * y } } }
  end

  def map2(a, b) = a.zip(b).map { |ra, rb| ra.zip(rb).map { |x, y| yield x, y } }

  class Network
    attr_reader :w0, :w1

    # sizes: [inputs, hidden, outputs]
    def initialize(sizes, seed: 1)
      rng = Random.new(seed)
      rand_matrix = ->(r, c) { Array.new(r) { Array.new(c) { rng.rand * 2 - 1 } } }
      @w0 = rand_matrix.(sizes[0], sizes[1])
      @w1 = rand_matrix.(sizes[1], sizes[2])
    end

    def forward(x)
      h = NeuralNet.dot(x, @w0).map { |r| r.map { |v| NeuralNet.sigmoid(v) } }
      o = NeuralNet.dot(h, @w1).map { |r| r.map { |v| NeuralNet.sigmoid(v) } }
      [h, o]
    end

    def predict(x) = forward(x).last

    # One full-batch step of backpropagation; returns mean absolute error.
    def train_step(x, y, lr)
      h, o = forward(x)
      err = NeuralNet.map2(y, o) { |t, p| t - p }
      d_o = NeuralNet.map2(err, o) { |e, p| e * NeuralNet.sigmoid_deriv(p) }
      h_err = NeuralNet.dot(d_o, @w1.transpose)
      d_h = NeuralNet.map2(h_err, h) { |e, p| e * NeuralNet.sigmoid_deriv(p) }
      @w1 = NeuralNet.map2(@w1, NeuralNet.dot(h.transpose, d_o)) { |w, g| w + lr * g }
      @w0 = NeuralNet.map2(@w0, NeuralNet.dot(x.transpose, d_h)) { |w, g| w + lr * g }
      err.flatten.sum { |e| e.abs } / err.flatten.size
    end

    def train(x, y, epochs:, lr: 0.5)
      err = nil
      epochs.times { err = train_step(x, y, lr) }
      err
    end
  end
end

if $PROGRAM_NAME == __FILE__
  xor_x = [[0, 0], [0, 1], [1, 0], [1, 1]]
  xor_y = [[0], [1], [1], [0]]
  net = NeuralNet::Network.new([2, 4, 1])
  err = net.train(xor_x, xor_y, epochs: 10_000)
  puts "final mean abs error: #{err.round(4)}"
  xor_x.zip(net.predict(xor_x)).each { |i, o| puts "#{i.inspect} -> #{o[0].round(3)}" }
end
