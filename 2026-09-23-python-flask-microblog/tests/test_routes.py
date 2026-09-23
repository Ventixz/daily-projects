from app.models import User


def register(client, username="alice", email="alice@example.com", password="secret1"):
    return client.post(
        "/register",
        data={
            "username": username,
            "email": email,
            "password": password,
            "password2": password,
        },
        follow_redirects=True,
    )


def login(client, username="alice", password="secret1"):
    return client.post(
        "/login",
        data={"username": username, "password": password},
        follow_redirects=True,
    )


def test_index_requires_login(client):
    resp = client.get("/", follow_redirects=True)
    assert b"Sign In" in resp.data


def test_register_creates_user(client, db):
    resp = register(client)
    assert resp.status_code == 200
    assert User.query.filter_by(username="alice").count() == 1


def test_register_duplicate_username_rejected(client, db):
    register(client)
    resp = register(client, email="other@example.com")
    assert User.query.filter_by(email="other@example.com").count() == 0
    assert b"different username" in resp.data


def test_login_wrong_password_rejected(client, db):
    register(client)
    resp = client.post(
        "/login",
        data={"username": "alice", "password": "wrong"},
        follow_redirects=True,
    )
    assert b"Invalid username or password" in resp.data


def test_login_then_post_appears_on_feed(client, db):
    register(client)
    login(client)
    resp = client.post("/", data={"post": "hello world"}, follow_redirects=True)
    assert resp.status_code == 200
    assert b"hello world" in resp.data


def test_logout_blocks_index(client, db):
    register(client)
    login(client)
    client.get("/logout")
    resp = client.get("/", follow_redirects=True)
    assert b"Sign In" in resp.data


def test_follow_unfollow_flow(client, db):
    register(client, username="alice", email="alice@example.com")
    register(client, username="bob", email="bob@example.com")

    login(client, username="alice")
    resp = client.post("/follow/bob", follow_redirects=True)
    assert b"You are following bob" in resp.data

    alice = User.query.filter_by(username="alice").first()
    bob = User.query.filter_by(username="bob").first()
    assert alice.is_following(bob)

    resp = client.post("/unfollow/bob", follow_redirects=True)
    assert b"You are not following bob" in resp.data
    assert not alice.is_following(bob)


def test_cannot_follow_self(client, db):
    register(client, username="alice", email="alice@example.com")
    login(client, username="alice")
    resp = client.post("/follow/alice", follow_redirects=True)
    assert b"cannot follow yourself" in resp.data
