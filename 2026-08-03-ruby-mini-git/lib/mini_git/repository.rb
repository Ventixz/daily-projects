require "fileutils"
require "time"

require_relative "object_store"
require_relative "tree_builder"

module MiniGit
  class Repository
    DEFAULT_AUTHOR = "MiniGit User <mini-git@example.com>".freeze

    attr_reader :work_dir, :git_dir, :store

    def self.init(work_dir)
      git_dir = File.join(work_dir, ".git")
      %w[objects refs/heads].each { |d| FileUtils.mkdir_p(File.join(git_dir, d)) }
      File.write(File.join(git_dir, "HEAD"), "ref: refs/heads/master\n")
      new(work_dir)
    end

    def initialize(work_dir)
      @work_dir = work_dir
      @git_dir = File.join(work_dir, ".git")
      @store = ObjectStore.new(@git_dir)
    end

    def write_tree
      TreeBuilder.new(@store).build(@work_dir)
    end

    def commit_tree(tree_sha, message, parent: nil, author: DEFAULT_AUTHOR, timestamp: Time.now)
      lines = ["tree #{tree_sha}"]
      lines << "parent #{parent}" if parent

      stamp = "#{timestamp.to_i} #{timestamp.strftime('%z')}"
      lines << "author #{author} #{stamp}"
      lines << "committer #{author} #{stamp}"
      lines << ""
      lines << message

      @store.write("commit", lines.join("\n") + "\n")
    end

    # Snapshots the whole working directory as a tree, then wraps it in a
    # commit object whose parent is whatever HEAD currently points at --
    # that single parent link is what turns a pile of snapshots into a
    # history.
    def commit(message, author: DEFAULT_AUTHOR, timestamp: Time.now)
      tree_sha = write_tree
      sha = commit_tree(tree_sha, message, parent: head_commit, author: author, timestamp: timestamp)
      update_head(sha)
      sha
    end

    def head_commit
      path = current_ref_path
      File.exist?(path) ? File.read(path).strip : nil
    end

    # Walks parent links from HEAD back to the first commit, newest first.
    # Returns an array of [sha, parsed commit fields] pairs.
    def log
      commits = []
      sha = head_commit

      while sha
        type, content = @store.read(sha)
        raise "expected commit object, got #{type}" unless type == "commit"

        parsed = parse_commit(content)
        commits << [sha, parsed]
        sha = parsed[:parent]
      end

      commits
    end

    private

    def parse_commit(content)
      header, message = content.split("\n\n", 2)
      fields = { message: message }

      header.each_line do |line|
        key, value = line.strip.split(" ", 2)
        fields[key.to_sym] = value if key
      end

      fields
    end

    def current_ref_path
      head = File.read(File.join(@git_dir, "HEAD")).strip
      ref = head.sub(/^ref: /, "")
      File.join(@git_dir, ref)
    end

    def update_head(sha)
      path = current_ref_path
      FileUtils.mkdir_p(File.dirname(path))
      File.write(path, sha + "\n")
    end
  end
end
