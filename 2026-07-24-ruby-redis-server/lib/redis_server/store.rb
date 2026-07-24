module RedisServer
  # Thread-safe in-memory key/value store with millisecond-precision TTLs.
  #
  # Expiry is lazy: a key past its deadline isn't reaped by a background
  # sweep, it's just treated as absent the next time anything looks it up
  # (get, exists?, or del). That's the same trick real Redis uses for most
  # of its keys - no timer thread, no scheduling, just a deadline check on
  # read.
  class Store
    def initialize
      @data = {}
      @deadlines = {}
      @mutex = Mutex.new
    end

    def set(key, value, ttl_ms: nil)
      @mutex.synchronize do
        @data[key] = value
        if ttl_ms
          @deadlines[key] = now_ms + ttl_ms
        else
          @deadlines.delete(key)
        end
      end
      nil
    end

    def get(key)
      @mutex.synchronize do
        expire(key)
        @data[key]
      end
    end

    def exists?(key)
      @mutex.synchronize do
        expire(key)
        @data.key?(key)
      end
    end

    # Deletes every key in `keys` and returns how many actually existed.
    def del(keys)
      @mutex.synchronize do
        keys.count do |key|
          expire(key)
          existed = @data.key?(key)
          @data.delete(key)
          @deadlines.delete(key)
          existed
        end
      end
    end

    private

    # Evicts `key` if its deadline has passed. Must be called with @mutex
    # already held.
    def expire(key)
      deadline = @deadlines[key]
      return unless deadline && deadline <= now_ms

      @data.delete(key)
      @deadlines.delete(key)
    end

    def now_ms
      Process.clock_gettime(Process::CLOCK_MONOTONIC, :millisecond)
    end
  end
end
