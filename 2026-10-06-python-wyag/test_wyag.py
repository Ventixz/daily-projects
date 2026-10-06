import os
import shutil
import subprocess
import tempfile
import unittest

import wyag
from wyag import GitError, Repo


def git(cwd, *args):
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, check=True).stdout.strip()


class Base(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.dir)
        self.repo = Repo.init(self.dir)

    def write(self, path, text):
        full = os.path.join(self.dir, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w") as f:
            f.write(text)


class TestObjects(Base):
    def test_blob_hash_matches_git(self):
        # `printf 'hello\n' | git hash-object --stdin`
        self.assertEqual(wyag.hash_blob(b"hello\n"), "ce013625030ba8dba906f756967f9e9ca394464a")

    def test_roundtrip_and_prefix(self):
        sha = self.repo.write_object("blob", b"data")
        self.assertEqual(self.repo.read_object(sha[:6]), ("blob", b"data"))

    def test_unknown_ref(self):
        with self.assertRaises(GitError):
            self.repo.resolve("nope")


class TestWorkflow(Base):
    def test_commit_log_and_nothing_to_commit(self):
        self.write("a.txt", "one\n")
        self.repo.add(["a.txt"])
        first = self.repo.commit("first")
        self.write("a.txt", "two\n")
        self.repo.add(["a.txt"])
        second = self.repo.commit("second")
        self.assertEqual([s for s, _ in self.repo.log()], [second, first])
        with self.assertRaises(GitError):
            self.repo.commit("again")

    def test_status(self):
        self.write("a.txt", "1\n")
        self.repo.add(["a.txt"])
        self.repo.commit("c")
        self.write("a.txt", "2\n")
        self.write("new.txt", "x\n")
        s = self.repo.status()
        self.assertEqual(s["modified"], ["a.txt"])
        self.assertEqual(s["untracked"], ["new.txt"])

    def test_branch_and_checkout(self):
        self.write("a.txt", "1\n")
        self.repo.add(["a.txt"])
        self.repo.commit("c1")
        self.repo.create_branch("feature")
        self.repo.checkout("feature")
        self.write("sub/b.txt", "b\n")
        self.repo.add(["sub"])
        self.repo.commit("c2")
        self.assertTrue(os.path.exists(os.path.join(self.dir, "sub", "b.txt")))
        self.repo.checkout("master")
        self.assertFalse(os.path.exists(os.path.join(self.dir, "sub")))
        self.repo.checkout("feature")
        self.assertTrue(os.path.exists(os.path.join(self.dir, "sub", "b.txt")))

    def test_checkout_refuses_dirty_tree(self):
        self.write("a.txt", "1\n")
        self.repo.add(["a.txt"])
        self.repo.commit("c1")
        self.repo.create_branch("x")
        self.write("a.txt", "dirty\n")
        with self.assertRaises(GitError):
            self.repo.checkout("x")


class TestRealGitInterop(Base):
    """Our repositories must be readable by the real git binary."""

    def setUp(self):
        super().setUp()
        try:
            git(self.dir, "--version")
        except (OSError, subprocess.CalledProcessError):
            self.skipTest("git not installed")

    def test_git_reads_our_repo(self):
        self.write("a.txt", "hello\n")
        self.write("dir/b.txt", "world\n")
        self.write("dir-x.txt", "sorts between\n")
        self.repo.add(["."])
        sha = self.repo.commit("interop")
        self.assertEqual(git(self.dir, "rev-parse", "HEAD"), sha)
        self.assertEqual(git(self.dir, "fsck", "--strict"), "")
        self.assertEqual(git(self.dir, "ls-files"), "a.txt\ndir-x.txt\ndir/b.txt")
        self.assertEqual(git(self.dir, "show", "HEAD:dir/b.txt"), "world")
        self.assertEqual(git(self.dir, "status", "--porcelain"), "")

    def test_we_read_git_repo(self):
        git(self.dir, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "--allow-empty", "-m", "x")
        self.write("f.txt", "git made this\n")
        git(self.dir, "add", "f.txt")
        git(self.dir, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-m", "real")
        self.assertEqual(set(self.repo.read_index()), {"f.txt"})
        self.assertEqual([c["message"].strip() for _, c in self.repo.log()], ["real", "x"])


if __name__ == "__main__":
    unittest.main()
