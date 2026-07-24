require_relative "test_helper"
require "socket"
require_relative "../server"

module RedisServer
  # End-to-end tests: a real TCPServer, real TCPSockets, real RESP bytes on
  # the wire - the same path a redis-cli or telnet session would take.
  class ServerTest < Minitest::Test
    def setup
      @server = Server.new(port: 0) # port 0 asks the OS for a free one
      @thread = Thread.new { @server.run }
      @socket = TCPSocket.new("127.0.0.1", @server.port)
    end

    def teardown
      @socket.close
      @server.close
      @thread.kill
    end

    def test_ping_over_the_wire
      @socket.write(RESP.encode(["PING"]))
      assert_equal "+PONG\r\n", read_reply
    end

    def test_set_and_get_over_the_wire
      @socket.write(RESP.encode(["SET", "foo", "bar"]))
      assert_equal "+OK\r\n", read_reply

      @socket.write(RESP.encode(["GET", "foo"]))
      assert_equal "$3\r\nbar\r\n", read_reply
    end

    def test_get_missing_key_over_the_wire
      @socket.write(RESP.encode(["GET", "nope"]))
      assert_equal "$-1\r\n", read_reply
    end

    def test_two_clients_see_the_same_data
      @socket.write(RESP.encode(["SET", "shared", "yes"]))
      read_reply

      other = TCPSocket.new("127.0.0.1", @server.port)
      begin
        other.write(RESP.encode(["GET", "shared"]))
        assert_equal "$3\r\nyes\r\n", read_reply(other)
      ensure
        other.close
      end
    end

    def test_inline_commands_work_too
      @socket.write("PING\r\n")
      assert_equal "+PONG\r\n", read_reply
    end

    private

    # A tiny RESP reader for the test side of the wire - reads exactly one
    # reply value, however it's typed, without needing to know its shape
    # up front.
    def read_reply(socket = @socket)
      header = socket.read(1) + socket.gets("\r\n")

      case header[0]
      when "$"
        length = header[1..-3].to_i
        return header if length == -1

        header + socket.read(length + 2)
      when "*"
        count = header[1..-3].to_i
        count.times.reduce(header) { |acc, _| acc + read_reply(socket) }
      else
        header
      end
    end
  end
end
