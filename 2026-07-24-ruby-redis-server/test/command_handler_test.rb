require_relative "test_helper"
require "redis_server/resp"
require "redis_server/store"
require "redis_server/command_handler"

module RedisServer
  class CommandHandlerTest < Minitest::Test
    def setup
      @handler = CommandHandler.new(Store.new)
    end

    def test_ping_with_no_message
      assert_equal RESP::SimpleString.new("PONG"), @handler.call(["PING"])
    end

    def test_ping_echoes_its_message
      assert_equal "hello", @handler.call(%w[PING hello])
    end

    def test_command_names_are_case_insensitive
      assert_equal RESP::SimpleString.new("PONG"), @handler.call(["ping"])
    end

    def test_echo_returns_its_argument
      assert_equal "hi", @handler.call(%w[ECHO hi])
    end

    def test_echo_requires_exactly_one_argument
      assert_instance_of RESP::Error, @handler.call(["ECHO"])
      assert_instance_of RESP::Error, @handler.call(%w[ECHO a b])
    end

    def test_set_then_get
      assert_equal RESP::OK, @handler.call(%w[SET foo bar])
      assert_equal "bar", @handler.call(%w[GET foo])
    end

    def test_get_on_missing_key_is_nil
      assert_nil @handler.call(%w[GET missing])
    end

    def test_set_with_ex_expires_the_key
      @handler.call(%w[SET foo bar EX 0])
      sleep 0.01
      assert_nil @handler.call(%w[GET foo])
    end

    def test_set_with_invalid_ttl_flag_is_a_syntax_error
      assert_instance_of RESP::Error, @handler.call(%w[SET foo bar NX])
    end

    def test_set_with_non_numeric_ttl_is_an_error
      assert_instance_of RESP::Error, @handler.call(%w[SET foo bar EX soon])
    end

    def test_del_returns_the_number_of_keys_removed
      @handler.call(%w[SET a 1])
      @handler.call(%w[SET b 2])

      assert_equal 2, @handler.call(%w[DEL a b missing])
    end

    def test_exists_counts_present_keys
      @handler.call(%w[SET a 1])

      assert_equal 1, @handler.call(%w[EXISTS a missing])
    end

    def test_unknown_command_is_an_error
      response = @handler.call(%w[FLUSHALL])
      assert_instance_of RESP::Error, response
      assert_includes response.message, "unknown command"
    end
  end
end
