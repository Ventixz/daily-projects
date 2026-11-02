# Learning: Build the game "Snake" in the terminal (Python)

**Source:** "Build the game 'Snake' in the terminal" (Robert Heaton) in the Python section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Scope: a pure, seedable game core (`Game`) plus a thin `curses` front end.

Run: `python3 snake.py` (arrows/WASD, `q` quits). Test: `python3 -m unittest`.

## Layout

- `snake.py` — `Game` (state, `turn`, `step`, `render`) and a `curses` `main`.
- `test_snake.py` — 12 tests: movement, reversal, walls, growth, self-collision, tail rule, determinism, win.

## What I learned

- **Separate the game from the terminal.** `Game` has no I/O, so every rule is unit-testable; curses only draws and reads keys.
- **A deque fits a snake:** push the new head left, pop the tail — O(1) per tick.
- **Tail-cell rule:** when not eating, the tail leaves this tick, so moving into it is legal. Collision must check the body *without* the tail (but with it when growing).
- **Ignore 180° turns** or a quick key press kills the snake instantly.
- **Seeded RNG** makes food placement reproducible in tests; choosing from free cells avoids retry loops and handles a full board (win).
- Mistake caught by tests: my own self-collision test steered into the tail, which is legal.

## Known limits

- No high scores, pause, or speed levels beyond a simple score-based delay; the curses loop itself is untested.
