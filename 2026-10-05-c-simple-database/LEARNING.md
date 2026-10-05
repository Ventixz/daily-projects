# Learning: A SQLite-style Database in C

**Source:** ["Let's Build a Simple Database"](https://cstack.github.io/db_tutorial/)
from the C/C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

One file (`db.c`), plain C11, no dependencies. Build with `make`, run `./db my.db`,
run the tests with `make test`.

## What it is

- **REPL / front end** — `insert <id> <username> <email>`, `select`, `find <id>`, plus
  meta-commands `.btree`, `.constants`, `.exit`. Validates IDs and string lengths.
- **Pager** — the file is an array of 4096-byte pages, loaded lazily into memory and
  flushed on exit. Data survives restarts.
- **B-tree** — rows live in leaf nodes, sorted by `id`. A full leaf splits in two; the
  root becomes an internal node that routes lookups by key. Leaves are chained with a
  `next_leaf` pointer so `select` is a linear scan in key order.
- **Cursor** — an abstraction over "a position in the table", used by `select`,
  `find` and `insert`.

```
page 0 (root, internal)         row = id(4) + username(33) + email(256) = 293 bytes
  [leaf A | key 7 | leaf B]     leaf cell = key(4) + row = 297 bytes -> 13 cells/page
     |-> next_leaf -> |
```

## What I learned

- **A database is a file format first.** Once every page has a defined layout
  (type byte, root flag, count, cells), "persistence" is just `write(fd, page)`.
- **Fixed-size rows make the page math trivial** but waste space: 293 bytes per row,
  13 per page. It's the reason real engines use variable-length cells and slot arrays.
- **Splitting is the whole game.** The easy part is the binary search inside a leaf.
  The fiddly part is the split: gather `MAX+1` logical cells into a scratch buffer
  (including the new one), then deal them into two nodes. Doing it through a temp
  buffer avoids shifting cells in place and mis-counting the insert position.
- **The root has to stay on page 0.** On the first split the old root's cells are copied
  to a *new* left child and page 0 is rewritten as an internal node, since the file
  header implicitly says "the table starts at page 0".
- **Internal-node keys are the max key of each child**, and the rightmost child has no
  key. Off-by-one risk lives in "child index == `num_keys` means right child".
- **Page pointers and growth.** `get_page` extending `num_pages` as a side effect made
  allocating two new pages in a row subtly order-dependent.
- **Testing a DB from the outside is cheap.** Piping a script into the binary and
  diffing stdout caught everything; the one failure I hit was my own test (the `db > `
  prompt shares a line with the first output).

## Verification

`make test` runs 7 end-to-end checks: insert/select, duplicate rejection, validation,
persistence across restarts, the exact tree shape after the first split, 500 shuffled
inserts returning in sorted order, and `find`. I also ran 6000 shuffled inserts under
AddressSanitizer + UBSan with no reports.

## Known limitations (by design)

- Tree depth is capped at 2: the root is an internal node whose children are all
  leaves, so an internal node never splits. Capacity is roughly 400 pages
  (~2.5k-5k rows); beyond that, inserts print `Error: Table full.`
- No delete, no update, no secondary indexes, no free-page list.
- Single process, no locking, no crash safety (a crash mid-flush can corrupt the file).
- The database is flushed on `.exit` or EOF, not on every write.

## License

MIT, see the repository root. Credit to the tutorial's author, Connor Stack.
