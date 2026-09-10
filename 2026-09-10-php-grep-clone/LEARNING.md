# Build Your Own `grep` (PHP)

**Source:** ["Build Your Own X" / "Build Your Own grep"](https://github.com/practical-tutorials/project-based-learning),
one of the general command-line-tool challenges listed in
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning)
(the same "reimplement a real Unix tool" idea behind the mini-git, shell, and
network-stack projects already in this repo, applied to `grep`).

Picked and built end-to-end in one sitting. Zero external dependencies —
standard library only (`preg_*`, `proc_open` for the CLI-level tests).

## What it is

A working subset of GNU `grep`, split so each concern lives in exactly one
class:

- `src/Options.php` — hand-rolled argv parser. Handles bundled short flags
  (`-inv` == `-i -v -n`), a flag that takes a glued-or-separate numeric
  argument (`-A3` and `-A 3` both work), long `--after-context=N` forms, and
  `--` to stop option parsing. No getopt() — grep's own flag grammar (glued
  numeric arguments in particular) doesn't map cleanly onto it.
- `src/PatternMatcher.php` — turns a pattern plus `-i`/`-w`/`-F` into one
  compiled PCRE and a single `matches(string $line): bool`. `-F` escapes the
  pattern with `preg_quote` before it ever reaches the regex engine, so
  `-F` and plain-regex mode share every other code path exactly.
- `src/Grep.php` — the actual point of the project: turns matched line
  numbers into the before/after-context blocks `-A`/`-B`/`-C` print, merging
  ranges that touch or overlap so a `--` separator only appears where a
  real gap exists between blocks.
- `src/Formatter.php` — renders a `SearchResult` into text. Never touches a
  file handle or STDOUT directly, so it's tested by asserting on returned
  strings, not by capturing output streams.
- `src/FileWalker.php` — expands `-r`/`-R` into a recursive directory walk;
  a directory passed without `-r` is reported and skipped, same as real
  `grep`.
- `bin/grep.php` — wires the pieces together and owns the only two things
  that vary between "reading a file" and "reading STDIN": the exit code
  (`0` = matched, `1` = no match, `2` = usage/read error) and whether
  filenames get printed at all (only when more than one file is searched).

## Run it

```bash
cd 2026-09-10-php-grep-clone
php tests/run_all.php                              # 41 tests, no Composer/PHPUnit needed

# recursive search with 1 line of context on each side, merged into one
# block because the two hits are close enough that their context overlaps
php bin/grep.php -rn -B1 -A1 ERROR fixtures/logs
#   3-2026-09-10 08:01:15 WARN  slow query took 812ms
#   4:2026-09-10 08:02:03 ERROR failed to write cache entry: timeout
#   5-2026-09-10 08:02:04 INFO  retrying cache write
#   6-2026-09-10 08:02:05 INFO  cache write succeeded
#   7:2026-09-10 08:05:41 ERROR connection reset by peer
#   8-2026-09-10 08:05:42 INFO  reconnecting

# recursive + count-only, one line per file searched
php bin/grep.php -rc TODO fixtures
#   fixtures/logs/app.log:0
#   fixtures/nested/deep/notes.txt:2
```

## What it actually teaches

- **Context lines are a range-merging problem, not a printing problem.**
  The naive approach — print `before` lines, print the match, print `after`
  lines, repeat per match — double-prints lines and puts spurious `--`
  separators between two hits that are only 2 lines apart with `-C3`. Real
  grep (and this implementation) instead converts every match into a
  `[start, end]` range first, sorts and merges ranges that touch
  (`start <= previous_end + 1`, not `start <= previous_end` — adjacency
  with a zero-line gap still merges), and only *then* walks the merged
  blocks to print. `--` becomes "there was a block boundary here," which
  is a fact about the merge, not something the printer has to decide.
- **`-v` (invert) and context don't compose the way you'd guess.** With
  `-v`, the lines that count as "hits" for context purposes are the
  *non*-matching ones — so `grep -v -C1` centers context windows around
  every non-match, not every match. Getting this right meant keeping one
  boolean set ("is this line selected") that both the context-range builder
  and the colon/dash printer read from, instead of asking the pattern
  matcher a second time at print time and getting a different answer.
- **A CLI's own argv grammar is usually irregular enough to write by hand.**
  `-A3`, `-A 3`, and `-inv` all have to work, and PHP's `getopt()` doesn't
  support glued numeric arguments after a flag group at all. Once the
  parser is loop-plus-switch instead of a library call, `-i`, `-n`, `-v`
  as a single bundled `-inv` and `-A` reading either the rest of its own
  argv token or the next one both fall out of the same character-by-character
  scan — no special-casing per flag needed beyond what each flag consumes.
