from datetime import datetime, timedelta, timezone

from app.models import User, Post


def test_password_hashing(db):
    u = User(username="susan", email="susan@example.com")
    u.set_password("cat")
    assert not u.check_password("dog")
    assert u.check_password("cat")


def test_password_hash_is_not_plaintext(db):
    u = User(username="susan", email="susan@example.com")
    u.set_password("cat")
    assert u.password_hash != "cat"


def test_follow(db):
    u1 = User(username="john", email="john@example.com")
    u2 = User(username="susan", email="susan@example.com")
    db.session.add_all([u1, u2])
    db.session.commit()

    assert u1.followed.count() == 0
    assert not u1.is_following(u2)

    u1.follow(u2)
    db.session.commit()

    assert u1.is_following(u2)
    assert u1.followed.count() == 1
    assert u1.followed.first().username == "susan"
    assert u2.followers.count() == 1


def test_follow_self_is_noop(db):
    u1 = User(username="john", email="john@example.com")
    db.session.add(u1)
    db.session.commit()

    u1.follow(u1)
    db.session.commit()
    assert u1.followed.count() == 0


def test_unfollow(db):
    u1 = User(username="john", email="john@example.com")
    u2 = User(username="susan", email="susan@example.com")
    db.session.add_all([u1, u2])
    db.session.commit()

    u1.follow(u2)
    db.session.commit()
    assert u1.is_following(u2)

    u1.unfollow(u2)
    db.session.commit()
    assert not u1.is_following(u2)
    assert u1.followed.count() == 0


def test_follow_posts(db):
    u1 = User(username="john", email="john@example.com")
    u2 = User(username="susan", email="susan@example.com")
    u3 = User(username="mary", email="mary@example.com")
    u4 = User(username="david", email="david@example.com")
    db.session.add_all([u1, u2, u3, u4])

    now = datetime.now(timezone.utc)
    p1 = Post(body="post from john", author=u1, timestamp=now + timedelta(seconds=1))
    p2 = Post(body="post from susan", author=u2, timestamp=now + timedelta(seconds=2))
    p3 = Post(body="post from mary", author=u3, timestamp=now + timedelta(seconds=3))
    p4 = Post(body="post from david", author=u4, timestamp=now + timedelta(seconds=4))
    db.session.add_all([p1, p2, p3, p4])
    db.session.commit()

    u1.follow(u2)
    u1.follow(u4)
    u2.follow(u3)
    u3.follow(u4)
    db.session.commit()

    f1 = [p.body for p in u1.followed_posts().all()]
    assert f1 == ["post from david", "post from susan", "post from john"]

    f2 = [p.body for p in u2.followed_posts().all()]
    assert f2 == ["post from mary", "post from susan"]

    f3 = [p.body for p in u3.followed_posts().all()]
    assert f3 == ["post from david", "post from mary"]

    f4 = [p.body for p in u4.followed_posts().all()]
    assert f4 == ["post from david"]
