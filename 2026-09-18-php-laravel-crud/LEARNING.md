# Build a Laravel CRUD Application From Scratch (PHP)

**Source:** ["Laravel CRUD Demo with Resource Controller Tutorial"](https://www.codewall.co.uk/laravel-crud-demo-with-resource-controller-tutorial/),
from the PHP section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial's brief is "wire up Laravel's resourceful routing and a
resource controller for one Eloquent model." I built a small library
loan tracker instead of the tutorial's usual to-do/post example
specifically because a `Book` has two fields (`status` and `due_date`)
that only make sense together — a `due_date` is meaningless on an
`available` book and mandatory on a `checked_out` one — so the form
request has to encode a real two-sided rule, not just per-field
`required`/`max` checks.

## What it is

- `database/migrations/..._create_books_table.php` — `title`, `author`,
  a unique `isbn`, an enum `status` (`available`/`checked_out`), and a
  nullable `due_date`.
- `app/Models/Book.php` — the Eloquent model, plus `isOverdue()` and
  `displayStatus()`. Both views (`index`, `show`) needed the same
  "is this checked-out book past its due date" check, so it lives once
  on the model instead of as duplicated `@php` blocks in Blade.
- `app/Http/Requests/StoreBookRequest.php` / `UpdateBookRequest.php` —
  form request validation. `due_date` carries `required_if:status,checked_out`
  *and* `prohibited_unless:status,checked_out` together, so a due date is
  rejected on an available book and required on a checked-out one — not
  just optional either way. `isbn` is unique, with `Rule::unique(...)->ignore($this->route('book'))`
  on update so a book doesn't collide with its own row.
- `app/Http/Controllers/BookController.php` — a plain resource controller
  (`index`, `create`, `store`, `show`, `edit`, `update`, `destroy`) wired
  to a single `Route::resource('books', BookController::class)` line.
- `resources/views/books/*.blade.php` — server-rendered Blade views
  (index, create, edit, show) sharing one `_form.blade.php` partial
  between create and edit, plus a small inline-styled layout. No Vite,
  no npm build — the default Laravel starter's Tailwind/Vite scaffolding
  was removed since nothing here needs a JS build step.
- `tests/Feature/BookCrudTest.php` — hits the actual HTTP routes
  (`RefreshDatabase` + sqlite in-memory) to exercise both the happy path
  and every validation edge case below.
- `tests/Unit/BookTest.php` — unit tests for `isOverdue()`/`displayStatus()`
  against a plain (unsaved) `Book` instance, no database involved.

## Run it

```bash
cd 2026-09-18-php-laravel-crud
composer install
cp .env.example .env
php artisan key:generate
touch database/database.sqlite
php artisan migrate
php artisan serve   # visit http://127.0.0.1:8000/books
```

```bash
php artisan test    # 14 tests: unit model logic + full HTTP CRUD flow
./vendor/bin/pint    # Laravel's code-style fixer, zero diffs on this tree
```

## What it actually teaches

- **A resource controller is a naming convention, not magic.** `Route::resource`
  just expands to seven `Route::get/post/put/delete` calls that map to
  `index`/`create`/`store`/`show`/`edit`/`update`/`destroy` by name — nothing
  stops you from writing those seven routes by hand, but the one-liner is
  what makes route-model binding (`Book $book` in `show`/`edit`/`update`/`destroy`)
  and `route('books.show', $book)`-style URL generation "just work" without
  each controller method re-deriving them.
- **`required_if` and `prohibited_unless` are opposite halves of one rule,
  and you need both.** `required_if:status,checked_out` alone still lets a
  request set `status=available` *and* send a `due_date` — Laravel happily
  saves it, and now an available book has a stale due date the UI has to
  special-case forever. Pairing it with `prohibited_unless:status,checked_out`
  closes that gap: the only way `due_date` legally has a value is when the
  status says it should.
- **A `nullable` field's rules still run against `null` unless you say
  `nullable`.** The `date` rule doesn't auto-skip an empty `due_date` when
  the book is `available` — omitting `nullable` had `store()` reject every
  available-book submission with "the due date field must be a valid
  date," even though `due_date` was correctly blank. `nullable` has to be
  listed explicitly for the validator to stop applying `date` to a value
  that isn't there. This only showed up once I drove the form through
  real HTTP requests in tests, not by reasoning about the rule list.
- **`Rule::unique()->ignore()` and "don't re-validate the field you didn't
  change" are different problems.** `isbn` is unique across books, but
  `UpdateBookRequest` still requires and validates it on every save — it
  just excludes the current row's own id from the uniqueness check via
  `->ignore($this->route('book'))`. Forgetting the `ignore()` makes every
  edit of an existing book fail uniqueness against itself.
- **A validation rule that's correct for `store` can be wrong for
  `update`.** `StoreBookRequest` rejects a `due_date` before today —
  reasonable when creating a new loan. `UpdateBookRequest` deliberately
  drops that rule: an already-overdue book (due date in the past, by
  definition) still needs to be editable, and reusing the create-time
  rule would make any overdue row permanently un-saveable except by
  deleting and recreating it. `test_update_allows_keeping_a_due_date_that_is_already_overdue`
  is the test that would catch a well-meaning "just reuse the store
  rules" refactor.

## Deliberate scope cuts

- **No authentication.** Every visitor can add, edit, and delete books;
  the tutorial is about resource controllers and form validation, not
  Laravel's auth scaffolding.
- **No borrower tracking.** `status`/`due_date` model *that a copy is
  out*, not *who has it* — there's no `members` table or loan history,
  since that's a second entity with its own CRUD, not a variation on
  this one.
- **SQLite, not MySQL/Postgres.** One file, zero services to run, and
  every rule exercised here (uniqueness, conditional requiredness, date
  comparisons) behaves identically across Laravel's supported drivers.

## What I'd add next

- **Search/filter on the index page** (`?status=checked_out`, title
  search) — currently `index()` just paginates everything.
- **A `php artisan book:overdue` console command** listing overdue books,
  reusing `Book::isOverdue()` outside the HTTP layer to show the same
  domain logic driving both a web view and a CLI report.
- **API resource endpoints** (`Route::apiResource` + `BookResource`)
  alongside the Blade views, to contrast Laravel's HTML-rendering
  resource controller with its JSON-only counterpart.
