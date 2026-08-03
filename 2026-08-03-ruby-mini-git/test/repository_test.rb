require_relative "test_helper"
require "mini_git/repository"

module MiniGit
  class RepositoryTest < Minitest::Test
    def setup
      @work_dir = Dir.mktmpdir
      @repo = Repository.init(@work_dir)
    end

    def teardown
      FileUtils.remove_entry(@work_dir)
    end

    def test_init_creates_the_git_directory_layout
      assert Dir.exist?(File.join(@work_dir, ".git", "objects"))
      assert Dir.exist?(File.join(@work_dir, ".git", "refs", "heads"))
      assert_equal "ref: refs/heads/master\n", File.read(File.join(@work_dir, ".git", "HEAD"))
    end

    def test_head_commit_is_nil_before_any_commit
      assert_nil @repo.head_commit
    end

    def test_commit_moves_head_to_the_new_commit
      File.write(File.join(@work_dir, "a.txt"), "v1")
      sha = @repo.commit("first commit")

      assert_equal sha, @repo.head_commit
    end

    def test_first_commit_has_no_parent
      File.write(File.join(@work_dir, "a.txt"), "v1")
      @repo.commit("first commit")

      _sha, fields = @repo.log.first
      assert_nil fields[:parent]
    end

    def test_second_commit_points_at_the_first_as_its_parent
      File.write(File.join(@work_dir, "a.txt"), "v1")
      first_sha = @repo.commit("first commit")

      File.write(File.join(@work_dir, "a.txt"), "v2")
      @repo.commit("second commit")

      newest, older = @repo.log
      assert_equal first_sha, newest[1][:parent]
      assert_nil older[1][:parent]
    end

    def test_log_returns_commits_newest_first_with_messages_intact
      File.write(File.join(@work_dir, "a.txt"), "v1")
      @repo.commit("first commit")
      File.write(File.join(@work_dir, "a.txt"), "v2")
      @repo.commit("second commit")

      messages = @repo.log.map { |_sha, fields| fields[:message].strip }
      assert_equal ["second commit", "first commit"], messages
    end

    def test_committing_unchanged_content_still_creates_a_new_commit_object
      File.write(File.join(@work_dir, "a.txt"), "v1")
      first_sha = @repo.commit("first commit")
      second_sha = @repo.commit("second commit, same tree")

      refute_equal first_sha, second_sha
      # but the tree they point at is identical, since nothing changed on disk
      _type, first_content = @repo.store.read(first_sha)
      _type2, second_content = @repo.store.read(second_sha)
      tree_of = ->(content) { content.lines.find { |l| l.start_with?("tree ") } }
      assert_equal tree_of.call(first_content), tree_of.call(second_content)
    end
  end
end
