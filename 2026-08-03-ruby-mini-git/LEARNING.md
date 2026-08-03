# Rebuilding Git in Ruby

**Source:** [Rebuilding Git in Ruby](https://thoughtbot.com/blog/rebuilding-git-in-ruby)
by thoughtbot, from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning) (Ruby section).

Picked and built end-to-end in one sitting, so this folder contains the finished implementation
directly at the project root (no separate `reference/`).

## What it is

A from-scratch reimplementation of Git's plumbing layer — the content-addressed object store and
commit graph that the porcelain commands (`git commit`, `git log`, ...) sit on top of:

- `lib/mini_git/object_store.rb` — reads and writes objects the same way real Git does: zlib-deflate
  `"<type> <byte size>\0<content>"`, key the file by the SHA1 of that (uncompressed) string, split the
  hex digest into a two-character directory and the remaining 38 characters.
- `lib/mini_git/tree_builder.rb` — recursively turns a working directory into a tree object: one
  `"<mode> <name>\0<20 raw sha bytes>"` entry per file/subdirectory, subdirectories becoming nested
  tree objects.
- `lib/mini_git/repository.rb` — `init`, `commit` (snapshot + link to parent), `log` (walk parent
  pointers from HEAD), ref/HEAD file handling.
- `mini_git.rb` — a CLI (`init`, `hash-object`, `cat-file`, `write-tree`, `commit-tree`, `commit`,
  `log`, `ls-tree`) so the object graph is directly poked at, the way `git cat-file -p <sha>` lets you
  poke at a real repo.
- `test/` — 17 assertions, including one that hashes a known example straight from Git's own
  internals documentation and asserts the exact SHA1, and one that runs `mini_git.rb write-tree`
  and real `git write-tree` over the *same* directory and asserts the hashes are byte-identical.

## Run it

```bash
cd 2026-08-03-ruby-mini-git
ruby test/run_all.rb      # 17 runs, 0 failures

mkdir /tmp/demo && cd /tmp/demo
ruby /path/to/mini_git.rb init
echo "hello" > a.txt
ruby /path/to/mini_git.rb commit -m "first commit"
ruby /path/to/mini_git.rb log
ruby /path/to/mini_git.rb cat-file -p <the-tree-sha-from-write-tree>
```

## What it actually teaches

- **A commit is a hash of a hash of hashes, and nothing more magical than that.** A blob is
  `sha1("blob <n>\0" + file bytes)`. A tree is `sha1("tree <n>\0" + entries)`, where each entry embeds
  the *sha* of a blob or another tree, not the content itself. A commit is `sha1("commit <n>\0" +
  "tree <sha>\nparent <sha>\n...")`. Once that clicks, "Git is a content-addressed filesystem with a
  commit graph bolted on" stops being a slogan and becomes exactly, literally what `object_store.rb`
  and `repository.rb` implement in about 100 lines combined.
- **Content-addressing gives you deduplication for free, and that's testable, not just a claim.**
  `test_identical_content_is_deduplicated_to_one_object` writes the same bytes twice and asserts
  exactly one file lands on disk — `ObjectStore#write` only calls `File.binwrite` when the hash's path
  doesn't already exist, so two files with identical content anywhere in the tree (or two commits with
  an unchanged file) automatically share one blob object.
- **Tree entries sort as if directories had a trailing slash, and getting this wrong silently
  produces a tree with the wrong hash.** `"foo.txt"` must sort *before* the directory `"foo"`, because
  Git compares `"foo.txt"` against `"foo/"` byte-for-byte and `.` (0x2E) sorts before `/` (0x2F) — a
  plain alphabetical sort of `["foo", "foo.txt"]` gets this backwards and every tree containing both a
  file and same-prefixed directory would hash differently from real Git's. `test_entries_are_sorted_as_if_directories_had_a_trailing_slash`
  pins this down directly, and I validated the whole builder against the real thing: `mini_git.rb
  write-tree` and `git write-tree` produce byte-identical SHA1s over the same directory (see the git
  history for this commit's testing).
- **HEAD is one level of indirection, not a hash.** `.git/HEAD` holds `ref: refs/heads/master`, not a
  commit sha directly — `commit` and `log` both resolve through that ref file rather than caching a
  sha anywhere, which is *why* Git can make branches cheap: creating one is just writing a new ref
  file that starts out pointing at the same commit as the one you branched from.
- **A parent pointer is the entire history mechanism.** `log` doesn't consult any separate history
  log — it reads the commit HEAD points to, follows its `parent <sha>` line to the previous commit,
  and repeats until a commit has no parent line. `test_second_commit_points_at_the_first_as_its_parent`
  and `test_committing_unchanged_content_still_creates_a_new_commit_object` together show the
  consequence: every `commit` call always creates a *new* commit object (new author/committer
  timestamp, new sha) even when the tree it points to is byte-identical to the last one, because a
  commit's identity includes "when" and "after what," not just "what."

## What I'd add next (stretch goals I skipped for scope)

- **A real index/staging area.** `commit` snapshots the entire working directory every time; there's
  no `add` to stage a subset of changes, so partial commits aren't possible.
- **Branches beyond `master`.** Ref handling already goes through a `refs/heads/<name>` file, but
  there's no `branch`/`checkout` to create or switch one.
- **Packfiles.** Every object is its own loose file forever; real Git periodically packs many objects
  into one delta-compressed file, which is most of where its actual storage efficiency comes from.
- **Diffing.** There's no equivalent of `git diff` — comparing two trees currently means reading both
  and eyeballing the entries by hand.
