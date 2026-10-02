# Learning: A Roguelike Engine in Rust

**Source:** ["Writing a Rust Roguelike for the Desktop and the Web"](https://aimlesslygoingforward.com/blog/2019/02/09/writing-a-rust-roguelike-for-the-desktop-and-the-web/)
from the Rust section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Built as a headless, std-only engine (no graphics crate, no dependencies). The
tutorial's desktop/web rendering is out of scope; this covers the game logic.

## What it is

- `src/lib.rs` — seeded RNG, rooms-and-corridors map generation, `Game` with
  player movement, melee combat, and monster chase AI, plus ASCII `render()`.
- `src/main.rs` — headless demo that prints the map before and after a scripted run.
- 7 unit tests inside `lib.rs`.

## What I learned

- **Seeded RNG = reproducible dungeons.** A small xorshift generator makes
  generation deterministic, which is what makes the generator testable.
- **Rooms and corridors:** place random rects, reject overlaps, join each room to
  the previous with an L-shaped corridor. Connectivity falls out of the construction;
  a BFS test over 29 seeds verifies it.
- **Flat `Vec<Tile>` with `y * width + x` indexing**, and keeping bounds checks in
  one `walkable()` function.
- **Borrow-checker ergonomics:** iterating monsters by index (`for i in 0..len`) lets
  me mutate `self.monsters[i]` and call `&self` helpers without fighting borrows.
- **Separating logic from rendering** again: no I/O in the library.

## Known limits

- Monster AI is greedy (dominant axis, then the other), not pathfinding, so monsters
  can get stuck behind walls. The demo player is equally naive, so in the demo run
  it never reaches an orc (hp 30, 8 orcs alive); combat is covered by unit tests instead.
- No field of view, items, or levels.

## Run it

```bash
cd 2026-10-02-rust-roguelike
cargo test   # 7 tests pass
cargo run    # prints the dungeon before/after a demo run
```
