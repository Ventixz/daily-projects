require 'stringio'
require_relative '../lib/network_stack'

$failures = []
$assertions = 0

def assert(condition, message = 'assertion failed')
  $assertions += 1
  raise message unless condition
end

def assert_equal(expected, actual, message = nil)
  assert(expected == actual, message || "expected #{expected.inspect}, got #{actual.inspect}")
end

def test(name)
  yield
  puts "  ok  #{name}"
rescue StandardError, RuntimeError => e
  $failures << [name, e]
  puts "FAIL  #{name}: #{e.message}"
end
