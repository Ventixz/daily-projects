require "fileutils"

module MiniGit
  # Turns a working directory into a "tree" object: a flat list of
  # "<mode> <name>\0<20 raw sha bytes>" entries, one per directory entry,
  # with subdirectories recorded as nested tree objects (so the whole
  # working directory collapses into a single content-addressed hash).
  class TreeBuilder
    IGNORED = [".git"].freeze

    def initialize(store)
      @store = store
    end

    # Returns the sha1 of the tree object for the given directory.
    def build(dir)
      names = Dir.children(dir).reject { |name| IGNORED.include?(name) }
      names.sort_by! { |name| sort_key(dir, name) }

      body = names.map { |name| entry_for(dir, name) }.join
      @store.write("tree", body)
    end

    private

    def entry_for(dir, name)
      path = File.join(dir, name)

      if File.directory?(path)
        mode = "40000"
        sha = build(path)
      else
        mode = File.executable?(path) ? "100755" : "100644"
        sha = @store.write("blob", File.binread(path))
      end

      "#{mode} #{name}\0#{[sha].pack("H*")}"
    end

    # Real git sorts tree entries as raw bytes, but treats directory names
    # as if they had a trailing "/" for the purpose of that comparison --
    # so "foo.txt" sorts before the directory "foo" (because "." < "/"),
    # even though a naive alphabetical sort would put "foo" first.
    def sort_key(dir, name)
      File.directory?(File.join(dir, name)) ? "#{name}/" : name
    end
  end
end
