# A Terminal Text Editor in Rust

**Source:** [hecto: Build your own text editor in Rust](https://www.flenker.blog/hecto/)
by Philipp Flenker (a Rust port of the classic [kilo](https://viewsourcecode.org/snaptoken/kilo/)
tutorial), from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning) (Rust section).

Picked and built end-to-end in one sitting, so this folder contains the finished implementation
directly at the project root.

## What it is

A `less`/`vim`-style terminal editor with raw-mode input handling, file open/save, cursor
movement, scrolling, and incremental search:

- `src/row.rs` — a single line: raw characters plus a cached, tab-expanded render string, with
  `insert`/`delete`/`split`/`append`/`find` as the primitive operations everything else composes
  from.
- `src/document.rs` — the buffer as a `Vec<Row>`: `open`/`save`, dirty tracking, and the
  document-level `insert`/`delete`/`find` that decide *which* row an edit or search touches (e.g.
  turning "delete at end of line" into "join the next row up").
- `src/terminal.rs` — the only module that imports `crossterm` directly; wraps raw-mode
  enable/disable, cursor positioning, and key reads so the rest of the editor speaks in
  `Position`s, not escape sequences.
- `src/editor.rs` — the event loop: keypress → cursor/document mutation → scroll → redraw, plus
  the save-as and incremental-search prompts and the "press Ctrl-Q three times" unsaved-changes
  guard.

## Run it

```bash
cd 2026-08-04-rust-text-editor
cargo test              # 15 unit tests, 0 failures — Row and Document logic, no terminal needed
cargo run -- some_file.txt
```

Keys: arrows/PageUp/PageDown/Home/End to move, type to insert, Backspace/Delete to remove,
Ctrl-S to save (prompts for a filename if the buffer has none), Ctrl-F for incremental search
(ESC cancels and restores the cursor), Ctrl-Q to quit (three presses if there are unsaved
changes).

I also drove it non-interactively through a pty (`os.fork` + `pty.fork` in Python, feeding raw
bytes to the child's stdin fd) to confirm the real keystroke path — not just the unit-tested
logic — actually writes to disk: open a file, send `!`, send Ctrl-S, send Ctrl-Q, then read the
file back and diff it. It came back as `!hello\n` for a `hello\n` input, confirming insert-at-cursor
and save both work through the full raw-terminal loop, not just in the `Document` unit tests.

## What it actually teaches

- **Terminal apps are just a loop over a stream of bytes, once you turn off the terminal's own
  processing of them.** Normal ("cooked") mode buffers a whole line and lets the kernel handle
  Backspace before your program ever sees a keystroke. Raw mode (`enable_raw_mode()` in
  `Terminal::new`) turns all of that off — no line buffering, no local echo, Ctrl-C delivered as
  a byte instead of a signal — which is *why* every key needs an explicit case in
  `process_keypress`: nothing is handled for you anymore.
- **A cursor position and a scroll offset are two different pairs of numbers, and conflating them
  is where off-by-one bugs live.** `cursor_position` is a coordinate in the *document*; `offset` is
  where in the document the *viewport's* top-left corner currently sits. `scroll()` only nudges
  `offset` when the cursor would otherwise land outside `[offset, offset + terminal_size)` — the
  cursor can be at document row 500 while the terminal only ever draws 40 rows at a time, and the
  same split is what makes horizontal scrolling on long lines work identically to vertical
  scrolling on a long file.
- **Deleting "at the end of a line" and deleting "a character" are different operations, and the
  document layer — not the row layer — has to be the one that knows which.** `Row::delete` only
  ever removes one character from one row; it has no way to reach into the next row. So
  `Document::delete` checks `at.x == row.len()` itself and, on that boundary, removes the *next*
  row and appends its content instead of calling `Row::delete` — the exact case
  `delete_at_end_of_line_joins_the_next_row` pins down. Getting this split wrong (e.g. putting
  the join logic in `Row`) would mean a `Row` could never be tested in isolation, because deleting
  off the end would require knowing about its neighbors.
- **String indexing by character, not by byte, matters the moment you touch tabs or non-ASCII
  content — and Rust's `&str` won't stop you from getting it wrong.** `Row` keeps `chars: Vec<char>`
  specifically so `insert(at, c)` and `render_slice(start, end)` can take *character* offsets; the
  render string, separately, expands tabs to a variable number of spaces, so a column on screen
  doesn't correspond 1:1 to either a byte or a char index into the raw content. `Row::find` has to
  bridge two more index spaces at once: `str::find` returns a *byte* offset into a *lowercased*
  haystack, which then has to be converted back to a char count before it means anything to the
  rest of the editor. `find_char_offset_survives_multibyte_content` (searching "world" in
  `"héllo world"`, where `é` is 2 bytes but 1 char) is the test that would catch this exact class
  of bug if the conversion were dropped.
- **A prompt is not a separate mode — it's the same event loop with a different callback.**
  `save()` and `search()` both call the one `prompt()` method, which re-renders the message bar
  and reads a key exactly like the main loop does, but hands each keystroke to a closure first.
  Save's callback is a no-op (it just wants the final string); search's callback re-runs
  `Document::find` after *every* keystroke, moving the cursor live — that's what makes it
  "incremental" rather than "search after Enter." Reusing the same input loop rather than writing
  a second one is what keeps ESC-to-cancel and Backspace-to-edit working identically in both
  prompts for free.

## What I'd add next (stretch goals I skipped for scope)

- **Syntax highlighting.** The tutorial's later chapters color `render` output by token type; this
  version renders every row in the terminal's default colors.
- **Multi-file / split views.** Only one `Document` exists at a time — no buffer list, no splits.
- **Undo/redo.** Every edit mutates `Document` in place; there's no history stack, so there's
  nothing to reverse.
- **Line-wrap for long lines.** Long lines currently scroll horizontally rather than wrapping,
  which matches `less`/`vim`'s default but not every editor's.
