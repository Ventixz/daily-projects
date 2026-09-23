# Microblog with Flask (Python)

**Source:** [Build a Microblog with Flask](https://blog.miguelgrinberg.com/post/the-flask-mega-tutorial-part-i-hello-world)
(The Flask Mega-Tutorial), from the Python section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting rather than scaffolded-then-attempted, so this folder
contains the finished implementation directly at the project root (no separate `reference/`).

## What it is

A small Twitter-style app: users register, log in, post short (280-char) updates, follow other
users, and see a feed of posts from people they follow plus their own. It's the application-factory
Flask pattern (`create_app`) rather than a single global `app` object, backed by SQLAlchemy over
SQLite.

- `app/__init__.py` -- the factory: builds the Flask app, wires up `SQLAlchemy` and `Flask-Login`,
  registers the `main` blueprint, and creates tables on startup.
- `app/models.py` -- `User` and `Post`, plus a `followers` association table for the self-referential
  many-to-many (a user follows other users). `User.followed_posts()` is the one query that matters:
  a `JOIN` against `followers` unioned with the user's own posts, ordered by timestamp.
- `app/forms.py` -- `Flask-WTF` forms with server-side validation, including custom validators
  (`validate_username`, `validate_email`) that hit the database to reject duplicates before a
  `UNIQUE` constraint would.
- `app/routes.py` -- everything lives on a `main` blueprint (not directly on `app`), so every
  `url_for()` call and the `login.login_view` setting need the `main.` prefix -- a detail that's
  easy to get wrong once and invisible until you test it.
- `tests/test_models.py` -- unit tests against an in-memory SQLite DB: password hashing, follow /
  unfollow, and the `followed_posts()` ordering across a 4-user graph.
- `tests/test_routes.py` -- integration tests via Flask's test client: register, login rejects a
  bad password, a post appears on the poster's own feed, logout locks the feed back down, and the
  follow/unfollow round trip.
- `e2e/smoke.py` -- boots the *real* `app.run()` dev server in a subprocess (not the test client)
  and drives it over plain HTTP with `requests` + cookie sessions: two real users, a CSRF token
  scraped out of the HTML like a browser would submit it, and the feed-visibility assertions that
  matter (bob can't see alice's post until he follows her).

## Run it

```bash
make setup   # pip install flask, flask-sqlalchemy, flask-login, flask-wtf, etc.
make run     # http://127.0.0.1:5000 -- register a user, post, follow someone else
```

```bash
make test    # unit + integration tests via pytest, in-memory SQLite, CSRF disabled
make e2e     # real dev server in a subprocess, driven over HTTP with real CSRF tokens
```

## What it actually teaches

- **CSRF protection is per-form, not app-wide, unless you opt in.** `Flask-WTF`'s `FlaskForm`
  only issues and checks a token for forms that render `{{ form.hidden_tag() }}` -- the plain
  `<form method="post">` used for follow/unfollow buttons isn't covered at all. That's a real
  security gap in the original tutorial's design (a CSRF-forged follow request would work), noted
  below as a stretch goal, and exactly the kind of thing that's invisible until an e2e test tries
  to `POST` without scraping a token and gets a 200 instead of a 400.
- **Blueprints change every endpoint name, including config strings.** Moving routes onto a
  `Blueprint("main", ...)` means `url_for("index")` silently becomes `url_for("main.index")`
  everywhere -- templates, redirects, and non-obviously, `login.login_view = "main.login"` in the
  app factory. Get the last one wrong and login still "works" until an anonymous user hits a
  protected page and Flask-Login can't build the redirect, which is a `BuildError` you only see
  under an actual unauthenticated request -- exactly what `test_index_requires_login` caught.
- **The union-of-two-queries feed pattern.** `followed_posts()` isn't "loop over followed users and
  concatenate" -- it's one SQL `JOIN` (posts by followed users) `UNION` (posts by self), sorted once
  by the database. That's the difference between a query that scales with follower count in the
  database engine versus in Python.
- **Test client vs. real server catch different bugs.** The Flask test client (`tests/test_routes.py`)
  calls straight into WSGI with no real HTTP, no cookie jar semantics, and CSRF turned off via
  `TestConfig` -- fast, but it would never have caught the CSRF-token requirement or a routing
  mistake that only shows up when a real `requests.Session` follows real redirects. `e2e/smoke.py`
  runs the literal `app.run()` entrypoint as a subprocess for that reason.

## What I'd add next (stretch goals I skipped for scope)

- App-wide `CSRFProtect(app)` instead of per-`FlaskForm` tokens, so the follow/unfollow buttons
  (and any future plain-POST route) are covered by default instead of by remembering to add a form.
- Pagination controls in the actual page UI (the pagination objects and routes already support
  `?page=N`; the templates render Newer/Older links but there's no page-number display).
- Rate-limiting posts and follow/unfollow actions -- nothing stops one authenticated user from
  posting or following in a tight loop right now.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Tutorial: ["Build a Microblog with Flask"](https://blog.miguelgrinberg.com/post/the-flask-mega-tutorial-part-i-hello-world)
(The Flask Mega-Tutorial, by Miguel Grinberg).
