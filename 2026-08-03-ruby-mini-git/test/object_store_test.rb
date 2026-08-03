require_relative "test_helper"
require "mini_git/object_store"

module MiniGit
  class ObjectStoreTest < Minitest::Test
    def setup
      @dir = Dir.mktmpdir
      @store = ObjectStore.new(@dir)
    end

    def teardown
      FileUtils.remove_entry(@dir)
    end

    def test_write_then_read_round_trips
      sha = @store.write("blob", "hello mini git")
      type, content = @store.read(sha)

      assert_equal "blob", type
      assert_equal "hello mini git", content
    end

    def test_hash_matches_real_git_for_a_known_blob
      # `echo -n "what is up, doc?" | git hash-object --stdin` is the
      # worked example from Git's own internals documentation.
      sha = @store.write("blob", "what is up, doc?")
      assert_equal "bd9dbf5aae1a3862dd1526723246b20206e5fc37", sha
    end

    def test_identical_content_is_deduplicated_to_one_object
      sha_a = @store.write("blob", "same bytes")
      sha_b = @store.write("blob", "same bytes")

      assert_equal sha_a, sha_b
      assert_equal 1, Dir.glob(File.join(@dir, "objects", "**", "*")).select { |f| File.file?(f) }.size
    end

    def test_read_raises_for_a_missing_object
      assert_raises(ObjectStore::MissingObjectError) { @store.read("0" * 40) }
    end

    def test_stored_object_is_actually_zlib_compressed_on_disk
      sha = @store.write("blob", "compress me")
      path = File.join(@dir, "objects", sha[0, 2], sha[2..])

      raw = File.binread(path)
      inflated = Zlib::Inflate.inflate(raw)

      assert_equal "blob 11\0compress me".b, inflated
      refute_equal inflated, raw
    end
  end
end
