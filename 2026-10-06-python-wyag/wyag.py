"""A small Git implementation (after "Write yourself a Git"), stdlib only.

Compatible with real Git's on-disk format: loose objects, index v2, refs.
Commands: init, hash-object, cat-file, add, write-tree, ls-tree, commit,
log, branch, checkout, status.
"""
import argparse
import hashlib
import os
import struct
import sys
import time
import zlib


class GitError(Exception):
    pass


# ---------------------------------------------------------------- repository
class Repo:
    def __init__(self, path="."):
        self.worktree = os.path.abspath(path)
        self.gitdir = os.path.join(self.worktree, ".git")

    @classmethod
    def init(cls, path="."):
        repo = cls(path)
        if os.path.exists(repo.gitdir):
            raise GitError(f"{repo.gitdir} already exists")
        for d in ("objects", "refs/heads", "refs/tags"):
            os.makedirs(os.path.join(repo.gitdir, d))
        repo.write_file("HEAD", b"ref: refs/heads/master\n")
        return repo

    @classmethod
    def find(cls, path="."):
        path = os.path.abspath(path)
        while True:
            if os.path.isdir(os.path.join(path, ".git")):
                return cls(path)
            parent = os.path.dirname(path)
            if parent == path:
                raise GitError("not a git repository")
            path = parent

    def p(self, *parts):
        return os.path.join(self.gitdir, *parts)

    def write_file(self, rel, data):
        full = self.p(rel)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "wb") as f:
            f.write(data)

    # ---- objects
    def write_object(self, kind, data):
        raw = f"{kind} {len(data)}".encode() + b"\0" + data
        sha = hashlib.sha1(raw).hexdigest()
        path = self.p("objects", sha[:2], sha[2:])
        if not os.path.exists(path):
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(zlib.compress(raw))
        return sha

    def read_object(self, sha):
        sha = self.resolve(sha)
        with open(self.p("objects", sha[:2], sha[2:]), "rb") as f:
            raw = zlib.decompress(f.read())
        header, _, data = raw.partition(b"\0")
        kind, size = header.split()
        if int(size) != len(data):
            raise GitError(f"object {sha}: bad length")
        return kind.decode(), data

    def resolve(self, name):
        """Full sha, unique sha prefix, branch name, or HEAD -> full sha."""
        if name == "HEAD":
            sha = self.head_commit()
            if sha is None:
                raise GitError("HEAD has no commits")
            return sha
        ref = self.read_ref("refs/heads/" + name)
        if ref:
            return ref
        if len(name) >= 4 and all(c in "0123456789abcdef" for c in name):
            d = self.p("objects", name[:2])
            hits = [name[:2] + f for f in os.listdir(d) if f.startswith(name[2:])] if os.path.isdir(d) else []
            if len(hits) == 1:
                return hits[0]
            if len(hits) > 1:
                raise GitError(f"ambiguous name {name}")
        raise GitError(f"unknown object or ref: {name}")

    # ---- refs
    def read_ref(self, ref):
        path = self.p(ref)
        if not os.path.isfile(path):
            return None
        with open(path) as f:
            return f.read().strip()

    def head_branch(self):
        """Branch name HEAD points at, or None when detached."""
        with open(self.p("HEAD")) as f:
            head = f.read().strip()
        return head[len("ref: refs/heads/"):] if head.startswith("ref: refs/heads/") else None

    def head_commit(self):
        b = self.head_branch()
        if b is None:
            with open(self.p("HEAD")) as f:
                return f.read().strip()
        return self.read_ref("refs/heads/" + b)

    def update_head(self, sha):
        b = self.head_branch()
        if b is None:
            self.write_file("HEAD", sha.encode() + b"\n")
        else:
            self.write_file("refs/heads/" + b, sha.encode() + b"\n")

    # ---- index (v2)
    def read_index(self):
        """Returns {path: (mode, sha)}."""
        path = self.p("index")
        if not os.path.exists(path):
            return {}
        with open(path, "rb") as f:
            data = f.read()
        if data[:4] != b"DIRC" or hashlib.sha1(data[:-20]).digest() != data[-20:]:
            raise GitError("corrupt index")
        version, count = struct.unpack(">II", data[4:12])
        if version != 2:
            raise GitError(f"unsupported index version {version}")
        entries, pos = {}, 12
        for _ in range(count):
            mode = struct.unpack(">I", data[pos + 24:pos + 28])[0]
            sha = data[pos + 40:pos + 60].hex()
            end = data.index(b"\0", pos + 62)
            name = data[pos + 62:end].decode()
            entries[name] = (mode, sha)
            pos += ((62 + len(name) + 8) // 8) * 8
        return entries

    def write_index(self, entries):
        out = b""
        for name in sorted(entries):
            mode, sha = entries[name]
            nb = name.encode()
            ent = struct.pack(">10I", 0, 0, 0, 0, 0, 0, mode, 0, 0, 0)
            ent += bytes.fromhex(sha) + struct.pack(">H", min(len(nb), 0xFFF)) + nb
            ent += b"\0" * (8 - len(ent) % 8)  # NUL-terminate and pad to 8
            out += ent
        out = b"DIRC" + struct.pack(">II", 2, len(entries)) + out
        with open(self.p("index"), "wb") as f:
            f.write(out + hashlib.sha1(out).digest())

    # ---- trees / commits
    def write_tree(self, entries=None):
        entries = self.read_index() if entries is None else entries
        return self._tree_for(entries, "")

    def _tree_for(self, entries, prefix):
        files, dirs = {}, set()
        for name, val in entries.items():
            if not name.startswith(prefix):
                continue
            rest = name[len(prefix):]
            if "/" in rest:
                dirs.add(rest.split("/")[0])
            else:
                files[rest] = val
        items = [(n, f"{m:o}", s) for n, (m, s) in files.items()]
        items += [(d, "40000", self._tree_for(entries, prefix + d + "/")) for d in dirs]
        # git sorts as if directory names ended in "/"
        items.sort(key=lambda i: i[0] + ("/" if i[1] == "40000" else ""))
        data = b"".join(m.encode() + b" " + n.encode() + b"\0" + bytes.fromhex(s) for n, m, s in items)
        return self.write_object("tree", data)

    def read_tree(self, sha):
        _, data = self.read_object(sha)
        out, pos = [], 0
        while pos < len(data):
            sp = data.index(b" ", pos)
            nul = data.index(b"\0", sp)
            out.append((data[pos:sp].decode(), data[sp + 1:nul].decode(), data[nul + 1:nul + 21].hex()))
            pos = nul + 21
        return out

    def flatten_tree(self, sha, prefix=""):
        """{path: (mode, sha)} for every blob under tree sha."""
        out = {}
        for mode, name, s in self.read_tree(sha):
            if mode == "40000":
                out.update(self.flatten_tree(s, prefix + name + "/"))
            else:
                out[prefix + name] = (int(mode, 8), s)
        return out

    def commit_tree(self, tree, parents, message, author="You <you@example.com>"):
        ts = f"{int(time.time())} +0000"
        lines = [f"tree {tree}"] + [f"parent {p}" for p in parents]
        lines += [f"author {author} {ts}", f"committer {author} {ts}", "", message]
        return self.write_object("commit", ("\n".join(lines) + "\n").encode())

    def parse_commit(self, sha):
        kind, data = self.read_object(sha)
        if kind != "commit":
            raise GitError(f"{sha} is a {kind}, not a commit")
        head, _, msg = data.decode().partition("\n\n")
        c = {"parents": [], "message": msg}
        for line in head.split("\n"):
            k, _, v = line.partition(" ")
            if k == "parent":
                c["parents"].append(v)
            else:
                c[k] = v
        return c

    # ---- worktree
    def tracked_files(self):
        for root, dirs, files in os.walk(self.worktree):
            dirs[:] = [d for d in dirs if d != ".git"]
            for f in files:
                yield os.path.relpath(os.path.join(root, f), self.worktree).replace(os.sep, "/")

    def blob_sha(self, relpath):
        with open(os.path.join(self.worktree, relpath), "rb") as f:
            return hash_blob(f.read())

    def add(self, paths):
        idx = self.read_index()
        for p in paths:
            full = os.path.join(self.worktree, p)
            targets = [p] if os.path.isfile(full) else [
                f for f in self.tracked_files() if f.startswith(p.rstrip("/") + "/") or p == "."]
            if not targets:
                raise GitError(f"pathspec '{p}' did not match any files")
            for t in targets:
                with open(os.path.join(self.worktree, t), "rb") as f:
                    sha = self.write_object("blob", f.read())
                mode = 0o100755 if os.access(os.path.join(self.worktree, t), os.X_OK) else 0o100644
                idx[t] = (mode, sha)
        self.write_index(idx)

    def commit(self, message):
        tree = self.write_tree()
        parent = self.head_commit()
        if parent and self.parse_commit(parent)["tree"] == tree:
            raise GitError("nothing to commit")
        sha = self.commit_tree(tree, [parent] if parent else [], message)
        self.update_head(sha)
        return sha

    def log(self, start="HEAD"):
        sha = self.resolve(start)
        while sha:
            c = self.parse_commit(sha)
            yield sha, c
            sha = c["parents"][0] if c["parents"] else None

    def branches(self):
        d = self.p("refs", "heads")
        return sorted(os.listdir(d)) if os.path.isdir(d) else []

    def create_branch(self, name):
        sha = self.head_commit()
        if sha is None:
            raise GitError("cannot branch: no commits yet")
        if self.read_ref("refs/heads/" + name):
            raise GitError(f"branch '{name}' already exists")
        self.write_file("refs/heads/" + name, sha.encode() + b"\n")

    def checkout(self, name):
        if self.status()["dirty"]:
            raise GitError("working tree has uncommitted changes to tracked files")
        sha = self.resolve(name)
        commit = self.parse_commit(sha)
        old = self.flatten_tree(self.parse_commit(self.head_commit())["tree"]) if self.head_commit() else {}
        new = self.flatten_tree(commit["tree"])
        for path in set(old) - set(new):
            full = os.path.join(self.worktree, path)
            if os.path.exists(full):
                os.remove(full)
                self._prune_dirs(os.path.dirname(full))
        for path, (mode, s) in new.items():
            full = os.path.join(self.worktree, path)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, "wb") as f:
                f.write(self.read_object(s)[1])
            os.chmod(full, 0o755 if mode == 0o100755 else 0o644)
        self.write_index(new)
        if self.read_ref("refs/heads/" + name):
            self.write_file("HEAD", f"ref: refs/heads/{name}\n".encode())
        else:
            self.write_file("HEAD", sha.encode() + b"\n")

    def _prune_dirs(self, d):
        while d != self.worktree and os.path.isdir(d) and not os.listdir(d):
            os.rmdir(d)
            d = os.path.dirname(d)

    def status(self):
        head = self.flatten_tree(self.parse_commit(self.head_commit())["tree"]) if self.head_commit() else {}
        idx = self.read_index()
        work = set(self.tracked_files())
        s = {"staged_new": [], "staged_mod": [], "staged_del": [],
             "modified": [], "deleted": [], "untracked": []}
        for p, (_, sha) in sorted(idx.items()):
            if p not in head:
                s["staged_new"].append(p)
            elif head[p][1] != sha:
                s["staged_mod"].append(p)
            if p not in work:
                s["deleted"].append(p)
            elif self.blob_sha(p) != sha:
                s["modified"].append(p)
        s["staged_del"] = sorted(set(head) - set(idx))
        s["untracked"] = sorted(work - set(idx))
        s["dirty"] = bool(s["modified"] or s["deleted"] or s["staged_new"] or s["staged_mod"] or s["staged_del"])
        return s


def hash_blob(data):
    return hashlib.sha1(f"blob {len(data)}".encode() + b"\0" + data).hexdigest()


# ----------------------------------------------------------------------- CLI
def main(argv=None):
    ap = argparse.ArgumentParser(prog="wyag")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("init")
    a = sub.add_parser("hash-object"); a.add_argument("file"); a.add_argument("-w", action="store_true")
    a = sub.add_parser("cat-file"); a.add_argument("object")
    a = sub.add_parser("add"); a.add_argument("paths", nargs="+")
    sub.add_parser("write-tree")
    a = sub.add_parser("ls-tree"); a.add_argument("tree")
    a = sub.add_parser("commit"); a.add_argument("-m", required=True)
    a = sub.add_parser("log"); a.add_argument("start", nargs="?", default="HEAD")
    a = sub.add_parser("branch"); a.add_argument("name", nargs="?")
    a = sub.add_parser("checkout"); a.add_argument("target")
    sub.add_parser("status")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "init":
            print(f"Initialized empty repository in {Repo.init().gitdir}")
            return 0
        repo = Repo.find()
        if args.cmd == "hash-object":
            with open(args.file, "rb") as f:
                data = f.read()
            print(repo.write_object("blob", data) if args.w else hash_blob(data))
        elif args.cmd == "cat-file":
            sys.stdout.buffer.write(repo.read_object(args.object)[1])
        elif args.cmd == "add":
            repo.add(args.paths)
        elif args.cmd == "write-tree":
            print(repo.write_tree())
        elif args.cmd == "ls-tree":
            for mode, name, sha in repo.read_tree(repo.parse_commit(repo.resolve(args.tree))["tree"]
                                                  if repo.read_object(args.tree)[0] == "commit" else args.tree):
                print(f"{mode:0>6} {sha} {name}")
        elif args.cmd == "commit":
            print(repo.commit(args.m))
        elif args.cmd == "log":
            for sha, c in repo.log(args.start):
                print(f"commit {sha}\n{c['author']}\n\n    {c['message'].strip()}\n")
        elif args.cmd == "branch":
            if args.name:
                repo.create_branch(args.name)
            else:
                cur = repo.head_branch()
                for b in repo.branches():
                    print(("* " if b == cur else "  ") + b)
        elif args.cmd == "checkout":
            repo.checkout(args.target)
        elif args.cmd == "status":
            s = repo.status()
            print(f"On branch {repo.head_branch() or '(detached)'}")
            for key, label in [("staged_new", "new file"), ("staged_mod", "staged modified"),
                               ("staged_del", "staged deleted"), ("modified", "modified"),
                               ("deleted", "deleted"), ("untracked", "untracked")]:
                for p in s[key]:
                    print(f"  {label}: {p}")
        return 0
    except (GitError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
