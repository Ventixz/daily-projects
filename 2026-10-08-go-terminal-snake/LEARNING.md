# Learning: Snake in the Terminal, in Go

**Source:** ["Build the game 'Snake' in the terminal"](https://robertheaton.com/2018/12/02/programming-project-5-snake/)
from the Python section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning),
reimplemented in Go (standard library only).

Play with `go run .` (wasd or hjkl, `q` quits); test with `go test ./...`.

## What it is

- `game.go`: pure game logic (`Game`, `Step`, `Turn`, `Render`), no I/O, injected RNG.
- `main.go`: the thin shell: `stty` for unbuffered input, a goroutine feeding keys into a
  channel, and a `select` loop over keys and a ticker.
- `game_test.go`: tests for movement, growth, walls, self-collision, winning and rendering.

## What I learned

- **Separate the rules from the terminal.** Because `Step` takes no I/O and the RNG is
  injected, every rule is testable without a TTY or sleeping.
- **The tail is not an obstacle.** On a non-growing tick the tail cell is vacated, so the
  head may legally move into it. Checking collisions against the full body is a classic
  off-by-one bug (there's a test for it). When eating, the tail stays, so it *is* checked.
- **Queue turns, judge reversal against the direction last travelled.** Pressing Up then
  Left within one tick would otherwise reverse the snake into its neck. `Turn` compares
  against the direction actually moved, and `Step` commits the queued one.
- **Place food by sampling free cells.** Retrying random cells until one is empty gets
  slow as the board fills; building the free list is O(cells) and always terminates. A
  full board is detected as a win before placing food (there'd be no free cell).
- **`select` makes a clean game loop.** One channel for keystrokes, one ticker for time;
  no polling, no shared mutable state between goroutines (only the main goroutine touches
  the game).

## Known limits

- Needs a Unix `stty`; no Windows support. Arrow keys (escape sequences) aren't parsed.
- Redraws the whole screen each event, which can flicker on slow terminals.
- Fixed speed and board size.
