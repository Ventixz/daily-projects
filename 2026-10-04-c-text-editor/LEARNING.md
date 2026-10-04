# Learning: A kilo-style Text Editor in C

**Source:** ["Build Your Own Text Editor"](http://viewsourcecode.org/snaptoken/kilo/)
from the C/C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Plain C11, no dependencies. Instead of one big file like the original, I split it in two:

- `editor.c` / `editor.h` — the **core**: row buffer, insert/backspace/newline, cursor
  movement, load/save, forward search. No terminal code, so it is unit tested.
- `kilo.c` — the **UI**: raw mode, escape-sequence key decoding, frame drawing,
  status bar, prompt, and the key bindings.
- `test_editor.c` — 30 checks on the core. Built with ASan + UBSan (`make test`).

Run it: `make && ./kilo notes.txt` — `Ctrl-S` save, `Ctrl-F` find, `Ctrl-Q` quit
(press twice with unsaved changes).

## What I learned

- **Raw mode is a handful of termios flags.** Turn off `ICANON` (line buffering),
  `ECHO`, `ISIG` (Ctrl-C/Z), `IXON` (Ctrl-S/Q flow control), `ICRNL` (so Enter is `\r`
  not `\n`) and `OPOST` (so I must print `\r\n` myself). Restore with `atexit`.
- **`VMIN=0, VTIME=1` makes Esc decodable.** Arrow keys arrive as `ESC [ A`. If the
  follow-up bytes don't show up within 100 ms, it was a bare Esc.
- **Build the frame in a buffer, write once.** Many small `write`s flicker; hide the
  cursor (`\x1b[?25l`), redraw, then show it again.
- **The "phantom last line".** The cursor may sit one row past the end of the file.
  Typing there must create a row first. Handling this in `ed_insert_char` is the
  difference between a crash and a working append.
- **`realloc` invalidates row pointers.** In `ed_insert_newline`, inserting a row
  reallocs the array, so the `Row *` taken before is stale. I re-fetch it. ASan
  would flag this immediately.
- **Splitting the core from the UI paid off.** Edge cases (backspace at 0,0; joining
  lines; clamping the column when moving to a shorter line; search wrap-around) became
  one-line tests rather than manual poking at a terminal.
- **Testing the real UI is still possible:** a Python `pty.fork()` script feeds keys
  (`hello\rworld`, arrows, Ctrl-S, Ctrl-Q) and checks the saved file. Doing it once
  caught nothing, but it was a good confidence check on the key decoding.

## Known limits

- Rows are stored as raw bytes: no tab rendering, no UTF-8 width handling.
- No syntax highlighting, incremental search, or undo (later kilo chapters / extras).
- Each character insert is a `realloc`; fine for a toy, not for large lines.
- Window size is read once at startup (no `SIGWINCH` handling).
- `ed_find` is a plain `strstr` per row, so it is case-sensitive.
