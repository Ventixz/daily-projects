# Learning: A Git in Python

**Source:** ["Write yourself a Git"](https://wyag.thb.lt/) from the Python section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

One file (`wyag.py`), Python 3 standard library only. Repositories it creates are
readable by the real `git` binary, and the other way round.

## What it is

- **Object store** — loose objects at `.git/objects/xx/yyyy…`, zlib-compressed
  `"<type> <size>\0<payload>"`, addressed by SHA-1.
- **Index** — reads and writes the binary index v2 format (`DIRC` header, 62-byte
  fixed entries + path, padded to 8 bytes, trailing SHA-1 checksum).
- **Trees and commits** — `write-tree` builds nested tree objects from the flat index;
  `commit` writes a commit and advances the current branch.
- **Porcelain** — `init`, `hash-object`, `cat-file`, `add`, `write-tree`, `ls-tree`,
  `commit`, `log`, `branch`, `checkout`, `status`.

## What I learned

- **Git is a content-addressed key/value store.** A blob is just its bytes plus a
  header; the filename lives in the tree, not the blob. Identical files share one object.
- **The index is the flat view, trees are the nested view.** `add` updates a sorted list
  of `path -> (mode, sha)`; `write-tree` folds it recursively into tree objects.
- **Tree sort order is subtle.** Git sorts entries as if directory names ended in `/`,
  so `dir-x.txt` sorts *before* the directory `dir`. Get it wrong and `git fsck --strict`
  complains, which is why the interop test uses exactly that layout.
- **A branch is a 41-byte file** containing a SHA. `HEAD` is a file pointing at a branch
  (or a raw SHA when detached). Committing = write object, overwrite one ref file.
- **Checkout is a diff of two flattened trees**: delete what the old tree had and the
  new one lacks, write everything in the new tree, rewrite the index.
- **Testing against the real tool is the best oracle.** `git fsck`, `ls-files`,
  `show` and `status --porcelain` all confirm byte-level compatibility.

## Known limits

- No packfiles (loose objects only), no merges, no tags, no `.gitignore`.
- Index entries store zeroed timestamps/inode data, so real git re-hashes files on
  `status` rather than using its stat shortcut.
- `checkout` refuses any uncommitted change instead of carrying compatible ones over.
- Author is hardcoded to `You <you@example.com>`, timestamps are UTC.

## Run it

```bash
cd 2026-10-06-python-wyag
python3 -m unittest -v          # 9 tests pass (2 need the git binary, else skipped)
mkdir /tmp/demo && cd /tmp/demo
python3 /path/to/wyag.py init
echo hi > a.txt && python3 /path/to/wyag.py add a.txt && python3 /path/to/wyag.py commit -m first
python3 /path/to/wyag.py log && git log      # real git reads it too
```
