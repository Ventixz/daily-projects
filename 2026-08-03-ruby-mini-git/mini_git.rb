#!/usr/bin/env ruby
# A minimal, from-scratch reimplementation of Git's plumbing: content-addressed
# blob/tree/commit objects, refs, and a HEAD pointer. See lib/mini_git for the
# actual logic -- this file is just command dispatch.

require_relative "lib/mini_git/repository"

def usage
  warn <<~USAGE
    Usage: mini_git.rb <command> [args]

    Commands:
      init                          Create a .git directory here
      hash-object [-w] <file>       Hash a file as a blob (optionally writing it)
      cat-file (-p|-t|-s) <sha>     Print/inspect a stored object
      write-tree                    Snapshot the working directory as a tree object
      commit-tree <tree> [-p p] -m msg   Create a commit object directly
      commit -m <message>           Snapshot + commit in one step, moves HEAD
      log                           Walk commit history from HEAD
      ls-tree <sha>                 List a tree object's entries
  USAGE
  exit 1
end

def repo
  MiniGit::Repository.new(Dir.pwd)
end

command, *args = ARGV
usage unless command

case command
when "init"
  MiniGit::Repository.init(Dir.pwd)
  puts "Initialized empty MiniGit repository in #{File.join(Dir.pwd, '.git')}"

when "hash-object"
  write = args.delete("-w")
  path = args.first
  usage unless path

  content = File.binread(path)
  sha = write ? repo.store.write("blob", content) : Digest::SHA1.hexdigest("blob #{content.bytesize}\0#{content}")
  puts sha

when "cat-file"
  mode, sha = args
  usage unless mode && sha

  type, content = repo.store.read(sha)
  case mode
  when "-p" then print content
  when "-t" then puts type
  when "-s" then puts content.bytesize
  else usage
  end

when "write-tree"
  puts repo.write_tree

when "commit-tree"
  tree_sha = args.shift
  usage unless tree_sha

  parent = nil
  message = nil
  until args.empty?
    case args.shift
    when "-p" then parent = args.shift
    when "-m" then message = args.shift
    else usage
    end
  end
  usage unless message

  puts repo.commit_tree(tree_sha, message, parent: parent)

when "commit"
  usage unless args.first == "-m" && args[1]
  puts repo.commit(args[1])

when "log"
  repo.log.each do |sha, fields|
    puts "commit #{sha}"
    puts "Author: #{fields[:author]}"
    puts ""
    puts fields[:message].to_s.each_line.map { |l| "    #{l}" }.join
    puts ""
  end

when "ls-tree"
  sha = args.first
  usage unless sha

  type, content = repo.store.read(sha)
  raise "not a tree: #{sha}" unless type == "tree"

  while content.bytesize.positive?
    header, rest = content.split("\0", 2)
    mode, name = header.split(" ", 2)
    entry_sha = rest.byteslice(0, 20).unpack1("H*")
    entry_type = mode == "40000" ? "tree" : "blob"
    puts "#{mode} #{entry_type} #{entry_sha}\t#{name}"
    content = rest.byteslice(20..)
  end

else
  usage
end
