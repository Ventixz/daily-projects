# minidb — a Simple Database (TypeScript)

**Source:** [Let's Build a Simple Database](https://cstack.github.io/db_tutorial/) by cstack, from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning)
(C/C++ section). The tutorial builds a small SQLite clone in C, ending in a full B-tree with
`WHERE` support across ~13 parts. This port covers the shape through roughly part 7 — REPL,
statement prepare/execute, fixed-width row (de)serialization, and a paged, file-backed store with
persistence — implemented in TypeScript instead of C. No B-tree yet: rows are appended in
insertion order and `select` is a linear scan, which is where I stopped rather than a limitation
of the language.

## What it is

A REPL (`minidb`) with three layers:

- `row.ts` — a fixed-width row schema (`id: uint32`, `username`/`email`: fixed-size byte ranges)
  with `serializeRow`/`deserializeRow` that pack/unpack a `Row` directly into a `Buffer` slice —
  the same layout a C `struct` would give you, but built by hand since JS/TS has no structs.
- `pager.ts` — a `Pager` owning the one open file descriptor and a page-sized (`4096` byte) cache.
  `getPage(n)` returns a cached `Buffer` or reads it from disk; `flushPage(n, size)` writes it back.
- `table.ts` — a `Table` that turns a row index into a `(page, offset)` slot via `ROWS_PER_PAGE`,
  and enforces `TABLE_MAX_ROWS` (`Table full.`).
- `statement.ts` / `main.ts` — `prepareStatement` tokenizes `insert`/`select` (hand-rolled, no SQL
  grammar), `executeStatement` runs them against a `Table`, and `main.ts` wires that to a
  `readline` REPL with `.exit` / `.constants` meta-commands.

## Run it

```bash
cd 2026-08-01-ts-simple-database
npm install
npm test            # builds, then runs every *.test.ts via node --test

npm start mydb.db    # interactive
db > insert 1 alice alice@example.com
Executed.
db > select
(1, alice, alice@example.com)
Executed.
db > .exit
```

Close and rerun `npm start mydb.db` — the row is still there, read back from the same file.

## What it actually teaches

- **A fixed-width row is a byte-offset contract, not a data type.** `row.ts` hardcodes
  `ID_OFFSET`/`USERNAME_OFFSET`/`EMAIL_OFFSET` and writes with `Buffer.writeUInt32LE` / `Buffer.write`
  at those exact offsets — this *is* what a C `struct Row { uint32_t id; char username[32]; char
  email[255]; }` plus `memcpy` gets you for free from the language. TypeScript has no struct layout,
  so the offsets are explicit constants instead of implicit from field declaration order. The payoff
  is the same either way: `ROW_SIZE` is fixed and known at compile time, so `Table` can compute
  `rowSlot(n)` with pure arithmetic — no length prefixes, no scanning for a previous row's end.
- **Zero-filling on write is what makes a shorter second value actually overwrite a longer first
  one.** `serializeRow` calls `page.fill(0, ...)` across the whole column width before writing the
  new string. Skip that and inserting `"bo"` over a slot that used to hold `"averylongusername"`
  would leave trailing bytes from the old value, and `deserializeRow`'s C-string-style scan-for-NUL
  would read past the new short string into that garbage. `row.test.ts`'s "zero-fills so a shorter
  second write erases the first" test is checking exactly this, and it fails immediately if the
  `page.fill` calls are removed.
- **A page cache and a file are different lifetimes, and conflating them corrupts partial pages.**
  My first version of `Pager` decided "is this page on disk?" from `fileLength / PAGE_SIZE`— an
  aligned-page count. That's wrong the moment the *last* page written is partial (which it always
  is, since `Table.close` only flushes `remainingRows * ROW_SIZE` bytes for the final page, not a
  full `4096`). With that check, reopening a database whose last page held fewer than
  `ROWS_PER_PAGE` rows silently read back zeroed rows for anything on that page — no error, just
  quietly wrong data. `table.test.ts`'s "rows survive a close and reopen" test (which spans a page
  boundary on purpose, `ROWS_PER_PAGE + 3` rows) caught this the first time I ran it. The fix:
  track `fileLength` directly and compute `bytesAvailable = fileLength - pageNum * PAGE_SIZE` per
  page, reading `min(PAGE_SIZE, bytesAvailable)` — the file's actual byte count is the source of
  truth, not a derived "how many full pages" count.
- **Validation belongs where the bad input first arrives, not where it would eventually break.**
  `prepareStatement` checks `id >= 0` and both string lengths *before* constructing a `Statement`,
  so `insert -1 ...` fails with `"ID must be positive."` at the parse step — never reaching
  `Table.insert`, which re-validates anyway via `validateRow` since it's a public method other
  callers could hit directly without going through the REPL parser. Two checks, not because one is
  redundant, but because they're guarding different boundaries: the REPL's textual input, and the
  `Table` API's structural input.
- **Separating "parse" from "run" is the same shape as every other project here that has a
  front-end/back-end split** (the lexer/parser/executor split in `2026-07-31-go-shell`, the
  scanner/parser/interpreter split in `2026-07-26-java-lox-interpreter`). `prepareStatement` returns
  a plain `Statement` union with no side effects, so `statement.test.ts` asserts on parse results
  without ever touching a `Table` — only `executeStatement` needs one.

## What I'd add next (stretch goals I skipped for scope)

- A real B-tree (the tutorial's parts 7–13): would replace the flat append-only row order with a
  sorted structure keyed on `id`, adding `Node`/leaf-and-internal-page splitting — a genuinely
  different data structure, not just more of the same paging code.
- `WHERE id = ...` / delete / update — right now `select` is an unconditional full scan because
  there's no index to seek with.
- Multi-column schemas — `Row` is hardcoded to exactly `(id, username, email)`; a real engine would
  parse a `CREATE TABLE` statement into a schema instead.
- Concurrent access / locking — only one process is ever assumed to hold the file descriptor.
