require_relative "test_helper"
require "redis_server/resp"
require "stringio"

module RedisServer
  class RESPTest < Minitest::Test
    def test_encode_simple_string
      assert_equal "+OK\r\n", RESP.encode(RESP::OK)
    end

    def test_encode_error
      assert_equal "-ERR boom\r\n", RESP.encode(RESP::Error.new("ERR boom"))
    end

    def test_encode_integer
      assert_equal ":42\r\n", RESP.encode(42)
    end

    def test_encode_bulk_string
      assert_equal "$5\r\nhello\r\n", RESP.encode("hello")
    end

    def test_encode_nil_is_null_bulk_string
      assert_equal "$-1\r\n", RESP.encode(nil)
    end

    def test_encode_array_of_mixed_types
      assert_equal "*2\r\n$3\r\nfoo\r\n:1\r\n", RESP.encode(["foo", 1])
    end

    def test_parse_command_reads_a_resp_array_of_bulk_strings
      io = StringIO.new("*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n")
      assert_equal ["GET", "foo"], RESP.parse_command(io)
    end

    def test_parse_command_accepts_inline_commands
      io = StringIO.new("PING\r\n")
      assert_equal ["PING"], RESP.parse_command(io)
    end

    def test_parse_command_returns_nil_at_eof
      io = StringIO.new("")
      assert_nil RESP.parse_command(io)
    end

    def test_parse_command_rejects_a_non_bulk_string_array_element
      io = StringIO.new("*1\r\n:5\r\n")
      assert_raises(RESP::ProtocolError) { RESP.parse_command(io) }
    end

    def test_parse_command_rejects_malformed_length
      io = StringIO.new("*x\r\n")
      assert_raises(RESP::ProtocolError) { RESP.parse_command(io) }
    end

    def test_round_trip_through_encode_and_parse
      wire = RESP.encode(["SET", "foo", "bar"])
      io = StringIO.new(wire)
      assert_equal ["SET", "foo", "bar"], RESP.parse_command(io)
    end
  end
end
