module RedisServer
  # Encodes Ruby values into RESP (REdis Serialization Protocol) wire bytes,
  # and parses RESP client requests off a socket.
  #
  # RESP has five reply types, each starting with a type byte:
  #   +text\r\n         simple string
  #   -message\r\n      error
  #   :123\r\n          integer
  #   $6\r\nfoobar\r\n  bulk string (byte length prefix, then the bytes)
  #   *2\r\n...         array of any of the above
  module RESP
    class ProtocolError < StandardError; end

    SimpleString = Struct.new(:text)
    Error = Struct.new(:message)

    OK = SimpleString.new("OK").freeze

    # Turns a Ruby value into the RESP bytes to send back to a client.
    def self.encode(value)
      case value
      when nil
        "$-1\r\n"
      when SimpleString
        "+#{value.text}\r\n"
      when Error
        "-#{value.message}\r\n"
      when Integer
        ":#{value}\r\n"
      when String
        "$#{value.bytesize}\r\n#{value}\r\n"
      when Array
        "*#{value.size}\r\n#{value.map { |item| encode(item) }.join}"
      else
        raise ArgumentError, "don't know how to encode #{value.class}"
      end
    end

    # Reads one client request off `io` and returns it as an array of
    # strings (command name first, then arguments) - the shape every
    # command takes regardless of how it arrived on the wire.
    #
    # Real clients send a RESP array of bulk strings:
    #   *2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n
    # Tools like `nc`/`telnet` send a plain line instead: "GET foo". Redis
    # itself accepts both ("inline commands"), so this does too. Returns
    # nil at end-of-stream (the client disconnected).
    def self.parse_command(io)
      line = read_line(io, allow_eof: true)
      return nil if line.nil?

      line.start_with?("*") ? parse_array(io, line) : line.split
    end

    def self.parse_array(io, header_line)
      count = parse_length(header_line)
      raise ProtocolError, "negative array length" if count.negative?

      count.times.map { parse_bulk_string(io) }
    end
    private_class_method :parse_array

    def self.parse_bulk_string(io)
      type_line = read_line(io)
      unless type_line.start_with?("$")
        raise ProtocolError, "expected bulk string, got #{type_line.inspect}"
      end

      length = parse_length(type_line)
      bytes = io.read(length)
      raise ProtocolError, "connection closed mid-bulk-string" if bytes.nil?

      io.read(2) # discard the trailing \r\n
      bytes
    end
    private_class_method :parse_bulk_string

    def self.parse_length(line)
      Integer(line[1..])
    rescue ArgumentError, TypeError
      raise ProtocolError, "malformed length in #{line.inspect}"
    end
    private_class_method :parse_length

    def self.read_line(io, allow_eof: false)
      line = io.gets("\r\n")
      if line.nil?
        return nil if allow_eof

        raise ProtocolError, "connection closed mid-command"
      end
      line.chomp("\r\n")
    end
    private_class_method :read_line
  end
end
