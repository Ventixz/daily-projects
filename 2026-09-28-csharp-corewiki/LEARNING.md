# Building the CoreWiki (C#)

**Source:** ["Building the CoreWiki"](https://www.youtube.com/playlist?list=PLVMqA0_8O85yC78I4Xj7z48ES48IQBa7p),
from the C# section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The series builds a wiki-style CMS in ASP.NET Core with Razor Pages; the
real [CoreWiki](https://github.com/csharpfritz/CoreWiki) project it produces
runs on Marten (a Postgres document store) and MediatR. I built a smaller,
from-scratch version on plain EF Core + SQLite instead, specifically because
that's where the interesting part of "a wiki" actually lives: what happens
when two people edit the same page at once, and how do you keep every past
version instead of just the current one. Both are relational-database
problems, not document-store ones, so a relational EF Core model teaches
them more directly than copying the real project's document-DB stack would.

## What it is

- `src/Models/WikiPage.cs` -- the current version of a page. `Version` is a
  plain `int` marked `[ConcurrencyCheck]`, not `[Timestamp]`: SQLite has no
  native auto-incrementing rowversion column the way SQL Server does, so
  `WikiService` bumps this by hand on every save and lets EF put the
  *previous* value in the `UPDATE ... WHERE Version = @old` clause.
- `src/Models/WikiRevision.cs` -- one immutable row per save. Nothing ever
  updates a row in this table; `WikiService` only ever inserts, which is
  what makes it a real history instead of just an "last edited by" field.
- `src/Services/WikiService.cs` -- `SaveAsync(slug, title, content, summary,
  expectedVersion)` is the whole app's write path. When `slug` is null it
  creates a page (with `UniqueSlugAsync` disambiguating a title collision
  into `-2`, `-3`, ...); when it's set, it loads the tracked row, sets
  `Entry(existing).Property(p => p.Version).OriginalValue = expectedVersion`,
  bumps `Version` to `expectedVersion + 1`, and appends a revision before
  calling `SaveChangesAsync`. If someone else saved in between, that update
  affects zero rows and EF throws `DbUpdateConcurrencyException`, which the
  service turns into a `SaveOutcome.Conflict` result carrying the *other*
  editor's now-current page instead of silently overwriting it.
- `src/Services/MarkdownRenderer.cs` -- Markdig with `.DisableHtml()`. Wiki
  content is written by whoever has edit access, which this project treats
  as untrusted input; without that call, a page could embed a `<script>`
  tag and it would render verbatim for every visitor.
- `src/Pages/Wiki/Edit.cshtml(.cs)` -- the edit form carries `Version` as a
  hidden field, seeded from whatever the editor's browser loaded. On a
  `Conflict` result it re-renders the form pre-filled with the *other*
  editor's saved content and a message telling the user to re-apply their
  change, rather than a generic "save failed."
- `src/Pages/Wiki/{View,New,History,Revision}.cshtml(.cs)` -- viewing a page
  renders its Markdown; History lists every revision newest-first; Revision
  renders one specific past version read-only, clearly marked as not the
  current page.
- `tests/TestSupport.cs` -- tests run against a real SQLite `:memory:`
  database over one open `SqliteConnection`, not EF's InMemory provider.
  The InMemory provider doesn't generate `WHERE`-clause SQL, so it can't
  exercise the actual mechanism the concurrency check depends on; real
  SQLite (even in memory) does the same conflict detection production would.
- `tests/WikiServiceTests.cs` -- `SaveAsync_rejects_a_stale_edit...` is the
  one that matters: two `WikiService` instances share one connection to
  stand in for two editors, both load version 1, one saves, and the second
  save (still carrying version 1) comes back as `Conflict` -- and the page
  on disk still has the first editor's content, not the second's.

## Run it

```bash
cd 2026-09-28-csharp-corewiki
dotnet test                                      # 12 tests, ~1s
cd src && dotnet run                             # http://localhost:5000 (or 5299 in dev)
```

The SQLite file (`wiki.db`) and its `-wal`/`-shm` siblings are created next
to `src/` on first run and are gitignored; delete them to reset the wiki
back to just the seeded Home page. No secrets or connection strings need to
be filled in -- `appsettings.json` points at a local file by default.

## What it actually teaches

- **A concurrency token doesn't need a database-generated column.** SQL
  Server's `[Timestamp]` rowversion is a convenience, not a requirement --
  any column EF can compare an "original" value against in a `WHERE` clause
  works the same way. A plain `int` that the application increments itself,
  marked `[ConcurrencyCheck]`, gets you the identical guarantee on SQLite,
  MySQL, or anywhere else that has no auto-updating column type.
- **Optimistic concurrency fails safe, not silent.** Without a version
  check, "last write wins" means the second save simply erases the first
  editor's change with no error and no trace. With it, the second save
  fails loudly (`DbUpdateConcurrencyException`), and the app can choose what
  "loudly" means to the user -- here, showing them the other edit instead of
  losing either one.
- **Immutable history is a modeling choice, not a database feature.**
  `WikiRevision` isn't special to EF Core or SQLite; it's an ordinary table
  that the code simply never issues an `UPDATE` against. The append-only
  guarantee comes entirely from `WikiService` only ever calling
  `Revisions.Add`, never `Revisions.Update` or touching an existing row.
- **Test infrastructure that's more "in-memory" than the real thing can
  hide the bug it's supposed to catch.** EF Core's InMemory provider looks
  like the fast, obvious choice for unit tests, but it doesn't run real SQL
  and so doesn't reproduce SQL's `WHERE`-clause-based concurrency failure.
  An in-memory *SQLite* database (real SQLite, temporary storage) is both
  fast and actually representative -- the same test would have passed
  against InMemory even with the `[ConcurrencyCheck]` attribute deleted.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Tutorial credit: ["Building the CoreWiki"](https://www.youtube.com/playlist?list=PLVMqA0_8O85yC78I4Xj7z48ES48IQBa7p) and [csharpfritz/CoreWiki](https://github.com/csharpfritz/CoreWiki).
