require_relative "test_helper"
require "redis_server/store"

module RedisServer
  class StoreTest < Minitest::Test
    def setup
      @store = Store.new
    end

    def test_get_on_missing_key_is_nil
      assert_nil @store.get("missing")
    end

    def test_set_then_get_round_trips
      @store.set("foo", "bar")
      assert_equal "bar", @store.get("foo")
    end

    def test_set_overwrites_an_existing_value
      @store.set("foo", "bar")
      @store.set("foo", "baz")
      assert_equal "baz", @store.get("foo")
    end

    def test_exists_reflects_presence
      refute @store.exists?("foo")
      @store.set("foo", "bar")
      assert @store.exists?("foo")
    end

    def test_del_removes_keys_and_returns_the_count_that_existed
      @store.set("a", "1")
      @store.set("b", "2")

      assert_equal 2, @store.del(%w[a b missing])
      assert_nil @store.get("a")
      assert_nil @store.get("b")
    end

    def test_key_expires_after_its_ttl
      @store.set("foo", "bar", ttl_ms: 10)
      assert @store.exists?("foo")

      sleep 0.05
      assert_nil @store.get("foo")
      refute @store.exists?("foo")
    end

    def test_set_without_ttl_clears_a_previous_ttl
      @store.set("foo", "bar", ttl_ms: 10)
      @store.set("foo", "baz")

      sleep 0.05
      assert_equal "baz", @store.get("foo"), "re-SET without a TTL should cancel the earlier expiry"
    end

    def test_expired_key_does_not_count_toward_del
      @store.set("foo", "bar", ttl_ms: 10)
      sleep 0.05

      assert_equal 0, @store.del(["foo"])
    end
  end
end
