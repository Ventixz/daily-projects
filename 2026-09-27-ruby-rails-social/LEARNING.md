# Build a Ruby on Rails Application (Ruby)

**Source:** ["How To Build a Ruby on Rails Application"](https://www.digitalocean.com/community/tutorials/how-to-build-a-ruby-on-rails-application),
from the Ruby on Rails section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial's brief is "scaffold a Rails app and wire up a resource end to
end." I built a small social network instead of the tutorial's own
single-resource example, specifically because Rails' `has_many :through`
only earns its keep on a *self-referential* association — a user
following other users — where the naive version (a `following_ids` array
column, or one `belongs_to` per direction) either can't enforce
uniqueness at the database level or can't tell "who follows me" from
"who do I follow" without two separate columns meaning two different
things.

## What it is

- `app/models/user.rb` -- `has_secure_password` (bcrypt) plus two
  `has_many :through` associations built on the *same* join model,
  `Relationship`, with different foreign keys: `active_relationships`
  (`foreign_key: "follower_id"`) gives `following`, and
  `passive_relationships` (`foreign_key: "followed_id"`) gives
  `followers`. `#feed` is one query --
  `Post.where(user_id: following.select(:id)).or(Post.where(user: self))`
  -- not a loop that hits the database once per followed user.
- `app/models/relationship.rb` -- both `belongs_to` point at `User` via
  `class_name:` (there's no `Follower` or `Followed` model). A uniqueness
  validation scoped to `[follower_id, followed_id]` stops a duplicate
  follow, and a custom `cannot_follow_self` validation stops a user
  following themselves -- something a plain DB unique index on the pair
  can't express, since `(5, 5)` is a perfectly unique pair.
- `app/models/like.rb` -- `belongs_to :post, counter_cache: :likes_count`.
  The counter lives on `posts.likes_count` and Rails maintains it
  automatically on `Like` create/destroy, so rendering a post's like
  count is a column read, not a `COUNT(*)` per post in the feed.
- `db/migrate/..._create_relationships.rb` -- `t.references :follower` /
  `:followed` default to inferring a `followers` / `followeds` table from
  the column name, which don't exist; both need an explicit
  `foreign_key: { to_table: :users }`, plus a unique index on
  `[follower_id, followed_id]` so the uniqueness validation has a real
  constraint backing it, not just an application-level check that a race
  condition can slip past.
- `app/controllers/posts_controller.rb` -- `#index` shows `current_user.feed`
  when signed in and `Post.recent.limit(20)` otherwise, so the same view
  and partial serve both a personalized feed and a public timeline.
- `app/controllers/relationships_controller.rb`,
  `app/controllers/likes_controller.rb` -- thin controllers; `create` /
  `destroy` do exactly one thing each (`current_user.follow(user)`,
  `@post.likes.find_or_create_by(user: current_user)`) because the actual
  rules (no self-follow, no duplicate like, counter maintenance) live on
  the models, not scattered across controller actions.
- `app/controllers/application_controller.rb` -- session-based auth with
  no gem: `current_user` memoizes `User.find_by(id: session[:user_id])`,
  and `require_login` is a `before_action` any controller opts into.
- `test/models/`, `test/integration/` -- model tests hit validations and
  associations directly (self-follow, duplicate like, counter cache
  increment/decrement, feed membership); integration tests drive real
  HTTP requests through `ActionDispatch::IntegrationTest` (sign up, sign
  in with a wrong password, post, follow, like/unlike, and the two
  authorization checks: a logged-out visitor can't post, and a user can't
  delete someone else's post).

## Run it

```bash
cd 2026-09-27-ruby-rails-social
bundle install
bin/rails db:prepare      # creates storage/{development,test}.sqlite3 and migrates both
bin/rails test            # 45 runs, model + integration
bin/rails db:seed         # optional: alice/bob/carol, all with password "password123"
bin/rails server          # http://localhost:3000
```

No `config/master.key` is committed (it's a secret, and this repo is
public) -- Rails transparently falls back to a locally-generated
development/test secret when credentials aren't present, so none of the
above needs it. It would only matter for a real production deploy.

## What it actually teaches

- **A self-join needs two `has_many :through` associations off the same
  join table, not one.** `following` and `followers` are both backed by
  `Relationship`, but from opposite ends -- `following` walks
  `active_relationships` (this user as `follower_id`) out to `followed`,
  while `followers` walks `passive_relationships` (this user as
  `followed_id`) back to `follower`. Naming the two `has_many
  :..._relationships` associations differently, even though they hit the
  same table, is what makes both directions queryable without the model
  confusing "who I follow" with "who follows me."
- **`references` in a migration guesses a table name from the column
  name, and a self-referential foreign key needs to override that
  guess.** `t.references :follower, foreign_key: true` tries to add a
  foreign key to a `followers` table, which doesn't exist -- both
  `follower_id` and `followed_id` point at `users`, so both need
  `foreign_key: { to_table: :users }` spelled out.
- **`counter_cache` is a write-time cost for a read-time win.** Every
  `Like.create`/`destroy` does one extra `UPDATE posts SET likes_count =
  ...`, but every place a post's like count is displayed -- the feed, a
  profile page, anywhere -- reads a plain column instead of running
  `posts.likes.count` per post. For a feed that's the difference between
  one query and N.
- **A uniqueness validation without a matching DB index is a race
  condition, not a guarantee.** `validates :follower_id, uniqueness: {
  scope: :followed_id }` only stops a duplicate if Rails happens to check
  before a concurrent request commits; the `add_index
  [:follower_id, :followed_id], unique: true` in the migration is what
  actually stops it at the database, and Rails' validation just turns the
  resulting DB error into a friendly `errors.add` instead of a raised
  exception the controller has to rescue.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Tutorial credit: ["How To Build a Ruby on Rails Application"](https://www.digitalocean.com/community/tutorials/how-to-build-a-ruby-on-rails-application), DigitalOcean.
