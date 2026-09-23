"""End-to-end smoke test: boots the real dev server (not the Flask test client)
in a subprocess and drives it over HTTP with requests + a cookie session,
exercising register -> login -> post -> follow -> feed end to end.
"""
import os
import re
import subprocess
import sys
import tempfile
import time

import requests

HOST = "127.0.0.1"
PORT = 5057
BASE = f"http://{HOST}:{PORT}"


def wait_for_server(proc, timeout=15):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError("server process exited before becoming ready")
        try:
            requests.get(BASE + "/login", timeout=1)
            return
        except requests.exceptions.ConnectionError:
            time.sleep(0.2)
    raise TimeoutError("server never became ready")


def csrf_token(html):
    match = re.search(r'name="csrf_token" type="hidden" value="([^"]+)"', html)
    assert match, "no csrf_token field found in form"
    return match.group(1)


def register(session, username, email, password="secret123"):
    r = session.get(BASE + "/register")
    r.raise_for_status()
    r = session.post(
        BASE + "/register",
        data={
            "csrf_token": csrf_token(r.text),
            "username": username,
            "email": email,
            "password": password,
            "password2": password,
        },
        allow_redirects=True,
    )
    assert r.status_code == 200, r.status_code
    return r


def login(session, username, password="secret123"):
    r = session.get(BASE + "/login")
    r.raise_for_status()
    r = session.post(
        BASE + "/login",
        data={
            "csrf_token": csrf_token(r.text),
            "username": username,
            "password": password,
        },
        allow_redirects=True,
    )
    assert r.status_code == 200, r.status_code
    assert "Invalid username or password" not in r.text
    return r


def main():
    env = os.environ.copy()
    with tempfile.TemporaryDirectory() as tmp:
        db_path = os.path.join(tmp, "e2e.db")
        env["DATABASE_URL"] = f"sqlite:///{db_path}"
        env["FLASK_DEBUG"] = "0"

        repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        proc = subprocess.Popen(
            [
                sys.executable,
                "-c",
                "from app import create_app; create_app().run("
                f"host='{HOST}', port={PORT})",
            ],
            cwd=repo_root,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        try:
            wait_for_server(proc)

            session_alice = requests.Session()
            register(session_alice, "alice", "alice@example.com")
            login(session_alice, "alice")

            feed = session_alice.get(BASE + "/")
            r = session_alice.post(
                BASE + "/",
                data={"csrf_token": csrf_token(feed.text), "post": "hello from alice"},
            )
            assert "hello from alice" in r.text, "alice's own post should show on her feed"

            session_bob = requests.Session()
            register(session_bob, "bob", "bob@example.com")
            login(session_bob, "bob")

            feed = session_bob.get(BASE + "/")
            r = session_bob.post(
                BASE + "/",
                data={"csrf_token": csrf_token(feed.text), "post": "hello from bob"},
            )
            assert "hello from bob" in r.text, "bob's own post should show on his feed"
            assert "hello from alice" not in r.text, "bob should not see alice's post yet (not following)"

            # /follow and /unfollow are plain POST forms (no FlaskForm), so
            # they aren't covered by Flask-WTF's per-form CSRF token.
            r = session_bob.post(BASE + "/follow/alice", allow_redirects=True)
            assert "You are following alice" in r.text

            r = session_bob.get(BASE + "/")
            assert "hello from alice" in r.text, "bob should see alice's post after following"
            assert "hello from bob" in r.text, "bob should still see his own post"

            r = requests.get(BASE + "/", allow_redirects=False)
            assert r.status_code == 302, "anonymous access to feed must redirect to login"

            print("E2E smoke test passed: register -> login -> post -> follow -> feed")
        finally:
            proc.terminate()
            try:
                out, _ = proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                out, _ = proc.communicate()
            if proc.returncode not in (0, None, -15):
                print(out)
                raise SystemExit(f"server exited abnormally: {proc.returncode}")


if __name__ == "__main__":
    main()
