require_relative "test_helper"
require "mini_git/object_store"
require "mini_git/tree_builder"

module MiniGit
  class TreeBuilderTest < Minitest::Test
    def setup
      @git_dir = Dir.mktmpdir
      @work_dir = Dir.mktmpdir
      @store = ObjectStore.new(@git_dir)
      @builder = TreeBuilder.new(@store)
    end

    def teardown
      FileUtils.remove_entry(@git_dir)
      FileUtils.remove_entry(@work_dir)
    end

    def test_builds_a_tree_with_one_blob_entry
      File.write(File.join(@work_dir, "a.txt"), "hello")

      sha = @builder.build(@work_dir)
      type, content = @store.read(sha)

      assert_equal "tree", type
      assert_includes content, "100644 a.txt\0"
    end

    def test_same_directory_contents_produce_the_same_tree_hash
      File.write(File.join(@work_dir, "a.txt"), "hello")
      sha_first = @builder.build(@work_dir)

      other_dir = Dir.mktmpdir
      begin
        File.write(File.join(other_dir, "a.txt"), "hello")
        sha_second = TreeBuilder.new(@store).build(other_dir)

        assert_equal sha_first, sha_second
      ensure
        FileUtils.remove_entry(other_dir)
      end
    end

    def test_ignores_the_dot_git_directory
      FileUtils.mkdir_p(File.join(@work_dir, ".git"))
      File.write(File.join(@work_dir, ".git", "HEAD"), "ref: refs/heads/master\n")
      File.write(File.join(@work_dir, "a.txt"), "hello")

      sha = @builder.build(@work_dir)
      _type, content = @store.read(sha)

      refute_includes content, ".git"
    end

    def test_subdirectories_become_nested_tree_objects
      FileUtils.mkdir_p(File.join(@work_dir, "sub"))
      File.write(File.join(@work_dir, "sub", "b.txt"), "nested")

      sha = @builder.build(@work_dir)
      _type, content = @store.read(sha)

      assert_includes content, "40000 sub\0"
    end

    def test_entries_are_sorted_as_if_directories_had_a_trailing_slash
      # "foo.txt" must sort before the directory "foo", because git compares
      # "foo.txt" against "foo/" byte-for-byte and '.' (0x2E) < '/' (0x2F) --
      # a plain alphabetical sort of ["foo", "foo.txt"] gets this backwards.
      FileUtils.mkdir_p(File.join(@work_dir, "foo"))
      File.write(File.join(@work_dir, "foo", "inner.txt"), "x")
      File.write(File.join(@work_dir, "foo.txt"), "y")

      sha = @builder.build(@work_dir)
      _type, content = @store.read(sha)

      assert content.index("foo.txt\0") < content.index("40000 foo\0")
    end
  end
end
