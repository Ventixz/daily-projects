require "digest/sha1"
require "zlib"
require "fileutils"

module MiniGit
  # Content-addressable storage, same on-disk shape as real Git's ".git/objects":
  # zlib-deflated "<type> <byte size>\0<content>", keyed by the SHA1 of that
  # (uncompressed) string, split into a two-character directory and the rest.
  class ObjectStore
    class MissingObjectError < StandardError; end

    def initialize(git_dir)
      @objects_dir = File.join(git_dir, "objects")
    end

    # Writes an object and returns its 40-character hex SHA1. Writing the
    # same content twice is a no-op the second time -- the hash already
    # tells you the object exists, so content-addressing makes storage
    # naturally deduplicated.
    def write(type, content)
      store = "#{type} #{content.bytesize}\0#{content}"
      sha = Digest::SHA1.hexdigest(store)
      path = object_path(sha)

      unless File.exist?(path)
        FileUtils.mkdir_p(File.dirname(path))
        File.binwrite(path, Zlib::Deflate.deflate(store))
      end

      sha
    end

    # Returns [type, content] for a stored object.
    def read(sha)
      path = object_path(sha)
      raise MissingObjectError, sha unless File.exist?(path)

      raw = Zlib::Inflate.inflate(File.binread(path))
      header, content = raw.split("\0", 2)
      type, _size = header.split(" ", 2)
      [type, content]
    end

    def exist?(sha)
      File.exist?(object_path(sha))
    end

    private

    def object_path(sha)
      File.join(@objects_dir, sha[0, 2], sha[2..])
    end
  end
end
