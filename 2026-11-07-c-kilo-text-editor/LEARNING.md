# Learning: Build Your Own Text Editor (C)

**Source:** "Build Your Own Text Editor" (kilo) in the C section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Scope: a kilo-style terminal editor in C, split into a testable editor core and a thin raw-mode front end. My own implementation of the idea, not a line-by-line copy of the tutorial.

Run: `make && ./kilo file.txt` (Ctrl-S save, Ctrl-F jump to next "TODO", Ctrl-Q quit). Test: `make test`.

## Layout

- `editor.c/.h` — text buffer as an array of rows; insert char, split line, backspace (joins rows), cursor movement with clamping/wrapping, open/save, find (wraps around), and a pure `ed_render` that turns the buffer into screen text.
- `main.c` — termios raw mode, escape-sequence arrow keys, redraw with ANSI codes, status bar.
- `test_editor.c` — asserts for typing, split/join, no-op backspace on empty buffer, cursor clamping, find wraparound, render clipping, and a save/open roundtrip. Built with ASan + UBSan.

## What I learned

- **Raw mode is a handful of termios flags**: clearing `ICANON`, `ECHO`, `ISIG`, `IXON`, `ICRNL`, `OPOST` hands you every byte; `VMIN=0/VTIME=1` makes `read` time out so lone Esc can be told apart from arrow-key sequences. Restoring in `atexit` matters or the shell is left broken.
- **Separating core from terminal made the editor testable.** Everything that edits text is a pure function over `Editor`, so no pty is needed for most tests (one pty smoke test covered the front end).
- **Rows as `realloc`'d arrays need care**: after `insert_row` reallocates `rows`, earlier `Row *` pointers are stale. I hit this in `ed_insert_newline` and re-fetch the pointer afterwards; ASan is what makes that class of bug loud.
- **Backspace at column 0 is a join, not a delete**, and cursor movement needs line-wrap rules at both ends.

## Known limits

- No tab expansion, UTF-8 awareness, horizontal scrolling, undo, or syntax highlighting.
- Find is hard-coded to "TODO" in the UI (the core `ed_find` takes any string); no incremental search prompt.
- The quit key does not warn about unsaved changes.
