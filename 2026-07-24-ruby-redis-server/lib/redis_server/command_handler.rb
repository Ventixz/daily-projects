module RedisServer
  # Turns a parsed command array (e.g. ["SET", "key", "value", "EX", "60"])
  # into a response value ready for RESP.encode. One store per handler,
  # shared across every connection, so a SET on one socket is visible to a
  # GET on another - the entire point of a key/value server.
  class CommandHandler
    def initialize(store)
      @store = store
    end

    def call(command)
      name, *args = command
      case name&.upcase
      when "PING" then ping(args)
      when "ECHO" then echo(args)
      when "SET" then set(args)
      when "GET" then get(args)
      when "DEL" then del(args)
      when "EXISTS" then exists(args)
      when nil then RESP::Error.new("ERR empty command")
      else RESP::Error.new("ERR unknown command '#{name}'")
      end
    end

    private

    def ping(args)
      case args.size
      when 0 then RESP::SimpleString.new("PONG")
      when 1 then args.first
      else RESP::Error.new("ERR wrong number of arguments for 'ping' command")
      end
    end

    def echo(args)
      return RESP::Error.new("ERR wrong number of arguments for 'echo' command") unless args.size == 1

      args.first
    end

    def set(args)
      key, value, *opts = args
      return RESP::Error.new("ERR wrong number of arguments for 'set' command") unless key && value

      ttl_ms = parse_ttl(opts)
      return ttl_ms if ttl_ms.is_a?(RESP::Error)

      @store.set(key, value, ttl_ms: ttl_ms)
      RESP::OK
    end

    def get(args)
      return RESP::Error.new("ERR wrong number of arguments for 'get' command") unless args.size == 1

      @store.get(args.first)
    end

    def del(args)
      return RESP::Error.new("ERR wrong number of arguments for 'del' command") if args.empty?

      @store.del(args)
    end

    def exists(args)
      return RESP::Error.new("ERR wrong number of arguments for 'exists' command") if args.empty?

      args.count { |key| @store.exists?(key) }
    end

    # Parses trailing SET options. Only EX (seconds) and PX (milliseconds)
    # are supported - real Redis has a dozen more (NX, XX, KEEPTTL, GET...)
    # but these two carry the "keys should expire" lesson on their own.
    def parse_ttl(opts)
      return nil if opts.empty?
      return RESP::Error.new("ERR syntax error") unless opts.size == 2

      flag, raw = opts
      case flag.upcase
      when "EX" then Integer(raw) * 1000
      when "PX" then Integer(raw)
      else RESP::Error.new("ERR syntax error")
      end
    rescue ArgumentError
      RESP::Error.new("ERR value is not an integer or out of range")
    end
  end
end
