# Learning: Terminal Tetris in C++

**Source:** ["Tetris Tutorial in C++ Platform Independent"](https://javilop.com/gamedev/tetris-tutorial-in-c-platform-independent-focused-in-game-logic-for-beginners/)
from the C/C++ section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial focuses on game logic rather than graphics, so I kept the logic separate
and drew it in a terminal with ANSI escapes.

C++17, standard library plus POSIX `termios`/`poll`. Build and run with `make && ./tetris`
(keys: `a`/`d` or arrows move, `w`/up rotate, `s`/down soft drop, space hard drop, `q` quit).
Run the tests with `make test`.

## Layout

- `tetris.hpp` — all game rules, no I/O: board, pieces, collision, rotation, line clears,
  scoring, 7-bag randomizer, gravity speed.
- `main.cpp` — raw-mode terminal input, `poll()` loop, ANSI rendering with a ghost piece.
- `test.cpp` — 181 checks on the logic.

## What I learned

- **Separate rules from rendering.** Because `Game` never prints or reads input, every
  rule is testable by setting up cells with `set_cell`/`set_piece` and calling one method.
- **Rotation is a coordinate transform.** Storing each piece once and rotating cells
  clockwise within its bounding box, `(x, y) -> (n-1-y, x)`, replaces 28 hand-typed
  rotation tables. `n` is 4 for I, 3 for most pieces, and O is special-cased.
- **Wall kicks are a cheap search.** If a rotation does not fit, try shifting by
  -1, +1, -2, +2. Simple, and enough to keep rotation usable near walls.
- **Clearing lines: re-check the same row.** After deleting a full row and shifting
  everything down, a new row occupies that index, so the loop must not advance.
- **7-bag randomizer.** Shuffling all seven pieces and dealing them out avoids long
  droughts that pure random produces.
- **Event loop with `poll()`.** Waiting on stdin with a timeout equal to the time left
  until the next gravity tick gives responsive input without busy-waiting or threads.
- **Restore the terminal.** Raw mode must be undone via `atexit`, or the shell is left
  with echo off.
- **Test setups can fool you.** My game-over test first filled whole rows, which
  cleared and let the next piece spawn. Leaving one column open fixed the scenario.

## Known limits

- No hold piece, no T-spin detection, no lock delay; pieces lock the moment they touch.
- Rotation uses simple kicks, not the official SRS tables.
- Input is plain `read()`; a lone Escape key press blocks until two more bytes arrive.
- Requires a POSIX terminal; it will not run on Windows without changes.
