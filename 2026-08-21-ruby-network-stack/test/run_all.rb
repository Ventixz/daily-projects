Dir[File.join(__dir__, '*_test.rb')].sort.each { |f| require_relative f }

puts
puts "#{$assertions} assertions, #{$failures.size} failures"
if $failures.any?
  $failures.each { |name, err| puts "  #{name}: #{err.class}: #{err.message}" }
  exit 1
end
