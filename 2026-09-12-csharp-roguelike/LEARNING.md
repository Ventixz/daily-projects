# Create a Rogue-like game in C#

**Source:** ["Create a Rogue-like game in C#"](https://roguesharp.wordpress.com/) by Faron Bracy
(the RogueSharp tutorial series), one of the C# entries in
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Built end-to-end in one sitting on .NET 8, using the [RogueSharp](https://www.nuget.org/packages/RogueSharp)
NuGet package (v4.2.0) for map generation, field-of-view, and pathfinding.

**One deliberate substitution:** the original series pairs RogueSharp with RLNET, an
OpenTK-based windowed renderer. That needs a display, which a headless container doesn't
have, so this version renders straight to `System.Console` instead. Same game logic, plain
text instead of a tileset window.

## What it is

A turn-based dungeon crawler split so each concern lives in one file under `src/`:

- `DungeonMap.cs` — wraps a RogueSharp `Map`, built from `RandomRoomsMapCreationStrategy`
  (random rooms joined by corridors), plus field-of-view.
- `Actor.cs` / `Player.cs` / `Monster.cs` — the `@`, and two monster kinds (`r` rats, `o`
  orcs), each with health, an attack-dice expression, and a flat defense score.
- `CombatSystem.cs` — rolls the attacker's dice, subtracts defense, floors at 1 damage.
- `GameEngine.cs` — the turn loop: move-or-attack, then every living monster either attacks
  (if adjacent) or paths toward the player (if it can see them), then a win/lose check.
- `Renderer.cs` — draws the map to the console: cells in the player's current view in full
  color, cells seen before but not now dimmed (fog of war), everything else blank.
- `Program.cs` — wires it together, reads arrow/WASD/hjkl input, and also has a `--demo`
  mode (see below).

## Run it

```bash
cd 2026-09-12-csharp-roguelike
dotnet run                      # interactive: arrows/WASD/hjkl to move, q to quit
dotnet test                     # 13 unit tests, xUnit
dotnet run -- --demo --seed 42  # scripted playthrough, no terminal needed
```

Interactive sample (fog of war: `#`/`.` in full color is currently visible, dim is
explored-but-not-visible, blank is unseen):

```
HP: 20/20    Turn: 0    Monsters left: 6
       #########
       #...@...#
       #.......#
       #.........
       #.......###
       #.......#
       #########
```

## What it actually teaches

- **Field-of-view and "explored" are two different bits, and RogueSharp only manages one of
  them.** `Map.ComputeFov(x, y, radius, true)` correctly updates `IsInFov` for every cell in
  view — but as of RogueSharp 4.2.0, it does *not* also set `IsExplored`, even though older
  versions of the library (and the tutorial's expectations) did. The bug this caused was
  exactly the kind that's easy to miss by eye: the very first render looked perfect, because
  everything currently visible renders identically whether or not it's separately marked
  "explored." It only showed up once the player walked far enough that a cell should have
  stayed dimly visible as fog of war after leaving sight — instead it snapped back to blank,
  and worse, a scripted "explore everything" driver looking for `!IsExplored` cells picked
  the player's own tile as a target forever, because it never got marked seen. Fixed by
  having `DungeonMap.RecomputeFov` walk the cells `ComputeFov` returns and explicitly
  `SetCellProperties(..., isExplored: true)` on each one. `DungeonMapTests` pins this down
  directly so it can't silently regress if the package changes behavior again.

- **"Can the monster see the player?" is a per-monster field-of-view question, and computing
  it honestly is expensive.** The textbook-correct approach recomputes FOV from every
  monster's own position every turn. This game takes a cheaper, deliberate shortcut instead:
  a monster "notices" the player exactly when the player's *own* FOV includes the monster's
  cell. Visibility is symmetric on an open map, so in practice this looks right, and it turns
  an O(monsters × map) computation into an O(1) lookup against FOV data already computed for
  rendering. The cost is a monster technically "seeing" through a diagonal wall gap it
  couldn't really see through — an acceptable trade for a small game, and the kind of
  approximation worth naming explicitly rather than presenting as ground truth.

- **A pathfinder is just a graph search that doesn't care who's asking.** `GameEngine`'s
  monster AI and `Program`'s own `--demo` auto-explorer both walk toward a target by asking
  the *same* `RogueSharp.PathFinder` for a path and taking one step of it per turn. Neither
  needed its own movement logic — "orc chases player" and "demo bot explores the nearest
  unseen room" are the identical operation (`ShortestPath(start, goal).StepForward()`) with a
  different goal cell plugged in.

- **Dice notation turns "roll 2d6+3" from a mini-parser you'd have to write into a library
  call.** `RogueSharp.DiceNotation.Dice.Roll("1d6+1", random)` takes the attack-dice *string*
  stored right on the actor and returns a result — no bespoke combat-math code, and because
  it accepts an `IRandom`, combat is exactly as testable as anything else here:
  `KnownSeriesRandom` hands back a fixed sequence of "rolls" instead of real randomness, so
  `CombatSystemTests` can assert an exact damage number instead of a range.

## Known limitations (by design, to keep scope to ~2-4 hours)

- Two monster types, one flat defense stat, no leveling, items, or inventory.
- No `--demo`-mode intelligence beyond "walk toward the nearest unseen tile, fight whatever's
  in the way" — it's a smoke test for the systems, not a strategy. It dies to monsters often;
  that's expected and fine.
- Monster "sight" is the symmetric-FOV shortcut described above, not real per-monster FOV.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Credit: ["Create a Rogue-like game in C#"](https://roguesharp.wordpress.com/) by Faron Bracy (RogueSharp).
