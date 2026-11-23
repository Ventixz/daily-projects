# Learning: The Ruby on Rails Tutorial sample app (Ruby, Rails 8)

**Source:** Michael Hartl, [*The Ruby on Rails Tutorial*](https://www.railstutorial.org/book), from the
Ruby on Rails section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
railstutorial.org was blocked from the build environment. This app follows the book's **sample app and
chapter list, not its text**. It is built on Rails 8.0 with no gems beyond what `rails new` adds, plus
`bcrypt`.

> Written by the daily-project routine (Claude Code). It was committed under the repo owner's account
> because they asked for that. The code is AI-written, not the owner's own attempt.

## What it is

A small Twitter-style microblog. It covers the book's whole arc:

| Book chapters | What's here |
|---|---|
| 3–5 static pages, layout | Home / Help / About / Contact, a shared header and footer, a `full_title` helper |
| 6 modelling users | `User` with validations, lowercase emails, and a **unique index** as well as a validation |
| 7 sign up | Signup form, error messages, strong parameters (a hand-added `admin` param is ignored) |
| 8–9 log in, remember me | Session login, a "remember me" cookie, logout |
| 10 updating, showing, deleting | Edit profile, friendly forwarding, a paginated users index, admin-only delete |
| 11 account activation | A one-time activation link sent by email. You can't log in until you use it |
| 12 password reset | A reset link sent by email that expires after 2 hours and works once |
| 13 microposts | 140-character posts on the profile page and the home feed, with delete for your own |
| 14 following users | Follow / unfollow, following and follower pages, and a feed of people you follow |

## Run it

```
bundle install
bin/rails db:setup        # creates the database, migrates, seeds 100 users (password: foobar)
bin/rails server          # http://localhost:3000, log in as example@railstutorial.org / foobar
bin/rails test            # 80 tests, about 1 second
```

On Windows 11 / PowerShell: `winget install RubyInstallerTeam.RubyWithDevKit.3.3`, open a new terminal,
then the same commands.

In development, emails are **written to `tmp/mails/<address>`** instead of being sent. To activate a new
account, open that file and paste the link into your browser. You can also preview both emails at
`/rails/mailers/user_mailer`. In production, set `SECRET_KEY_BASE` and configure SMTP.

## The idea that ties it together: tokens are passwords

Chapters 9, 11 and 12 look like three features. They are really **one pattern used three times**:

1. Make a random token: `SecureRandom.urlsafe_base64` gives 128 bits.
2. Give the **raw token** to the user only, in a cookie or an emailed link.
3. Store only the **bcrypt digest** in the database (`remember_digest`, `activation_digest`, `reset_digest`).
4. To check a token, compare it to the digest with `BCrypt::Password#is_password?`.

Because of step 3, a leaked `users` table can't be turned into working remember cookies or reset links.
The same is true of passwords. One method, `User#authenticated?(attribute, token)`, checks all three
kinds by building the column name (`"#{attribute}_digest"`). A test reads the raw database row and
checks that no raw token is in it.

The emailed links carry the email address as well (`/account_activations/<token>/edit?email=…`). The
token can't be looked up directly, because the database only holds its digest. So the app finds the user
by email, then checks the token against that user's digest.

## How login works

- `log_in` calls **`reset_session`** first. That gives the browser a new session id, so an id an
  attacker planted before login (session fixation) stops working. A test checks that the id changes.
- "Remember me" uses two permanent cookies: an encrypted user id and the raw remember token.
  `current_user` tries the session first, then the cookies.
- The session also stores a **session token**, which is the current remember digest. Logging out clears
  the digest, so **every other browser logged in as you is logged out too**. A test logs in from two
  sessions, logs out of one, and checks that the other is refused.
- Logging in without ticking "remember me" also clears the digest. This matches the book: it ends
  remember-me logins on other computers.
- A finished password reset clears the reset digest (so the link works once) **and** the remember digest
  (so anyone holding an old cookie is logged out).

## Things the book leaves as exercises, done here

- Activation and reset links **work only once**. Each digest is cleared after use.
- The password reset form gives **the same answer** whether or not the email has an account. It sends
  no email to unknown or unactivated addresses, so the form can't be used to find out who is registered.
- The login error is the same for a wrong email and a wrong password.
- Friendly forwarding stores **only GET requests** and only redirects to a local path (`/…`, not `//…`).
- Admins can't delete themselves. Profiles of unactivated users aren't shown.
- You can only delete **your own** posts and follows. The app looks them up through `current_user`, so
  someone else's post or follow comes back as "not found".
- `following?` runs one `EXISTS` query and doesn't load the whole list. The feed is
  **one SQL query**: a subselect of the people you follow, `OR` your own posts. A test counts the
  queries.
- Both uniqueness rules (email, and one follow per pair) are also **unique indexes** in the database.
  Tests insert duplicates with validations skipped and expect `RecordNotUnique`.

## Differences from the book

- **No extra gems.** The book uses `will_paginate`, Bootstrap, Faker and `rails-controller-testing`.
  Here, pagination is about 15 lines (`app/controllers/concerns/pagination.rb`), the CSS is hand-written,
  seeds use a word list, and tests use `assert_select` instead of `assert_template`.
- **No JavaScript.** Logout, delete, follow and unfollow use `button_to` forms with real
  DELETE and POST requests. They don't need Turbo or rails-ujs.
- **No image uploads on microposts** (the book's Active Storage section). Active Storage was left out
  of the app.
- Rails 8.0 conventions: failed form submissions return `422` and redirects after changes return `303`.

## Tests (80 runs, 600 assertions)

- **Models (30):** validations, email format and case, DB uniqueness, the token lifecycle (activation,
  remember, reset expiry and single use), no raw tokens in the database, follow and unfollow (including
  duplicates and following yourself), feed contents, and the feed running as one query.
- **Mailers and helpers (3):** subject, recipient, the token and escaped email in each link, and `full_title`.
- **Integration (47):** signup with activation pulled from the delivered email (wrong token, wrong
  email, reuse), login and logout (including logging out twice), session fixation, remember cookies
  (real, forged, after the session is gone), logout ending other sessions, edit authorization and
  forwarding, admin delete, the users index (pagination, page clamping, hiding unactivated users),
  the full password reset (expiry, empty and mismatched passwords, single use, remember-me sessions
  ended), the micropost interface (HTML escaping, can't delete others' posts), following pages, and
  the feed.

A browser check also passed in Chromium: sign up, open the activation link from `tmp/mails`, post,
follow the admin, see their posts in the feed, log out.

## Layout

```
app/models/user.rb                 tokens + digests, activation, reset, feed, follow
app/models/{micropost,relationship}.rb
app/helpers/sessions_helper.rb     log_in, remember, current_user, forwarding
app/controllers/                   users, sessions, account_activations, password_resets,
                                   microposts, relationships, static_pages, concerns/pagination.rb
app/mailers/user_mailer.rb         account_activation, password_reset (HTML + text)
db/migrate/                        users, microposts, relationships (+ indexes and foreign keys)
test/                              models, mailers, helpers, integration, fixtures
```

## What to try next

- Add image uploads to microposts with Active Storage, with checks on file type and size.
- Throttle repeated login and password-reset attempts by IP address and by email.
- Re-send the activation email, and expire activation links the way reset links expire.
