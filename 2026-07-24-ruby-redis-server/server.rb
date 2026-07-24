#!/usr/bin/env ruby
require "socket"
require_relative "lib/redis_server/resp"
require_relative "lib/redis_server/store"
require_relative "lib/redis_server/command_handler"

module RedisServer
  # Accepts connections on `port` and serves them forever, one thread per
  # connection, all sharing a single Store.
  class Server
    def initialize(port:, host: "127.0.0.1")
      @tcp_server = TCPServer.new(host, port)
      @handler = CommandHandler.new(Store.new)
    end

    # The bound port - useful when `port: 0` asked the OS to pick one.
    def port
      @tcp_server.addr[1]
    end

    def run
      loop do
        client = @tcp_server.accept
        Thread.new { serve(client) }
      end
    rescue IOError, Errno::EBADF
      # #close was called (e.g. shutdown) while #accept was blocked; exit quietly.
    end

    def close
      @tcp_server.close
    end

    private

    def serve(client)
      loop do
        command = RESP.parse_command(client)
        break if command.nil? || command.empty?

        client.write(RESP.encode(@handler.call(command)))
      end
    rescue RESP::ProtocolError => e
      client.write(RESP.encode(RESP::Error.new("ERR Protocol error: #{e.message}")))
    rescue Errno::ECONNRESET, Errno::EPIPE
      # client hung up mid-write; nothing left to do
    ensure
      client.close
    end
  end
end

if __FILE__ == $PROGRAM_NAME
  port = (ENV["PORT"] || 6379).to_i
  server = RedisServer::Server.new(port: port)
  puts "redis_server listening on 127.0.0.1:#{server.port}"
  server.run
end
