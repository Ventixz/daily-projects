# Learn C# By Building a Simple RPG Game (C#)

**Source:** ["Learn C# By Building a Simple RPG Game"](http://scottlilly.com/learn-c-by-building-a-simple-rpg-index/)
by Scott Lilly, from the C# section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The original tutorial is a WinForms desktop app with an XML save file, built
up chapter by chapter. I kept the domain it teaches -- a player with
stats/inventory/equipment, monsters with loot tables, locations gated behind
quests, turn-based combat, a vendor -- and dropped the two layers that
aren't about that domain: no WinForms (a text console instead) and no save
file (a session lives and dies with the process). What's here is a fresh
design against the same brief, not a port of the tutorial's C# to a
different UI.

## What it is

A text-adventure RPG: `look`, `move`/`n`/`s`/`e`/`w`, `attack`, `flee`,
`inventory`, `equip`, `use`, `accept`/`turnin` a quest, `buy`/`sell` at a
vendor. Five locations (Home, Town Square, Farmer's Field, Farmer's Barn,
Spider Forest), two quests, three monster types.

- `src/Models/` -- `Player`, `Monster`, `MonsterTemplate`, `Item` (and its
  `Weapon`/`Armor`/`Consumable`/`QuestObjectiveItem` subtypes), `Inventory`,
  `Quest`/`PlayerQuest`.
- `src/World/` -- `Location` (exits, gating rules, vendor stock, monster
  template, quest) and `GameWorld` (the hand-built map).
- `src/Combat/` -- `CombatEngine`, driven entirely through an injected
  `IRandomSource` rather than `System.Random` directly.
- `src/Game.cs` -- the only class that knows what a "command" is.
  `ProcessCommand(string) -> string`, no `Console` calls anywhere in it.
- `src/Program.cs` -- the actual `Console.ReadLine`/`WriteLine` loop; thin
  on purpose, see below.
- `tests/` -- 76 hand-rolled assertions, no xunit/NUnit. The test project
  compiles `src/**/*.cs` directly (`RpgGame.Tests.csproj`, `Exclude`s
  `Program.cs` so its top-level-statement `Main` doesn't collide with the
  test runner's) rather than referencing a separately built assembly.

## Run it

```bash
cd 2026-08-11-csharp-rpg-game
make test    # 76 assertions, no NuGet packages beyond the SDK's own
make run     # interactive console session
```

## What it actually teaches

- **Combat has to go through an interface, not `System.Random`, or the
  interesting rules become untestable.** `CombatEngine` takes an
  `IRandomSource` in its constructor; `SystemRandomSource` wraps `Random`
  for real play, `FakeRandomSource` (`tests/FakeRandomSource.cs`) replays a
  fixed queue for tests. That single seam is what makes
  `CombatEngineTests.KillingBlowGivesMonsterNoRetaliation` possible at
  all: queue exactly `[8]` against an 8-HP monster and assert the monster
  gets no counter-swing -- a property that's real but essentially
  unobservable through a live `Random`, where you'd need thousands of runs
  to even notice a rare edge case behaves correctly.
- **A monster template has to be a factory, not a shared instance, or
  killing one monster kills it for every future visitor.** My first draft
  had `Location.Monster` hold a `Monster` directly. The bug: two players
  (or one player re-entering the field) would fight the *same* object, so
  a monster killed once stays dead forever, HP frozen at 0, for the whole
  process lifetime. `MonsterTemplate.Spawn()` (`src/Models/Monster.cs`)
  calls a `Func<Monster>` factory instead, and
  `CombatEngineTests.SpawnedMonstersAreIndependentInstances` pins it by
  killing one spawn and asserting the next spawn from the same template
  comes back at full HP.
- **A grinding integration test found a real balance bug before a human
  player would have.** `GameFlowTests.QuestLifecycleGrindsRatsTurnsInAndUnlocksBarn`
  originally queued 3 damage per rat retaliation hit (three hits/rat, no
  healing between fights). It looked fine on paper; running it showed the
  player dying mid-fight against the *third* rat -- 9 damage/rat × 2 rats
  already fought left the player at 2 HP against a fresh 8-HP monster with
  no way to have healed in between (there's no free heal-over-time, and the
  quest reward that would `AddExperience` a level-up heal doesn't land until
  *after* all three rats are dead). That's not a test bug to shrug off --
  it's the actual shape of the game for a level-1 character with no armor
  and no potions, and it's the reason the sequence in that test now uses 1
  damage/hit with a comment explaining why, rather than silently going back
  to "whatever number makes the test pass."
- **Damage mitigation needs an explicit floor, or "better armor" can
  produce "unkillable".** `Player.TakeDamage`
  (`src/Models/Player.cs`) computes `Math.Max(1, rawDamage - DefenseBonus)`.
  `PlayerTests.DefenseNeverReducesDamageBelowOne` arms a
  999-defense item against 5 raw damage and asserts exactly 1 HP is lost --
  without the floor, sufficiently stacked armor makes every fight in the
  game unwinnable *for the monster*, which is a balance failure mode a
  damage-number-only test wouldn't catch, only a specifically adversarial
  one would.
- **Not every "turn in this item" objective should consume the item.**
  `QuestObjective` (`src/Models/Quest.cs`) carries a
  `ConsumedOnCompletion` bool per objective, not a single flag on the whole
  quest. `Rat Tail`s are proof-of-kill and vanish on turn-in; a
  hypothetical "prove you carry a Sturdy Pickaxe" objective wouldn't
  (`QuestTests.ConsumedObjectiveIsRemovedButKeptObjectiveIsNot` exercises
  exactly that two-objective, one-consumed-one-kept case at the model
  layer, independent of `Game.TurnInQuest`'s own use of the same loop).
- **A location can need two different kinds of "not yet" -- and they check
  different things.** `Location.IsAccessibleTo`
  (`src/World/Location.cs`) has two independent gates:
  `LocationToVisitFirstId` (have you ever stepped foot somewhere) and
  `RequiredCompletedQuestId` (have you finished a specific quest). Spider
  Forest uses the first (you have to have walked through the barn) and the
  barn uses the second (the rat quest has to be *done*, not just accepted).
  `WorldTests` tests them as two separate methods precisely because merging
  them into one "has the player earned access" boolean would hide that a
  quest gate needs `player.FindQuest(id)?.IsCompleted`, not
  `VisitedLocationIds.Contains`, and the two aren't interchangeable.
- **Keeping `Game` free of `Console` calls is what makes a played session a
  unit test.** `Game.ProcessCommand(string) -> string` never touches
  stdin/stdout; `Program.cs` is the only file that does, and it's an 18-line
  loop. `GameFlowTests.QuestLifecycleGrindsRatsTurnsInAndUnlocksBarn` plays
  a genuine multi-fight, multi-location, quest-accept-and-turn-in session
  as a list of strings in and assertions on `Player` state out -- the same
  separation the Java HTTP server project in this repo used for
  `Router`/`Handler` versus the socket accept loop, applied here to a REPL
  instead of a network server.

## Deliberate scope cuts

- **No save/load.** The tutorial's XML serialization is a real, separate
  skill (schema versioning, migrating old saves); a session here lives for
  one `dotnet run` and that's the whole scope.
- **No WinForms/graphics.** The tutorial is UI-framework-first; this
  project is domain-model-first, so the interface is the thinnest thing
  that can drive it.
- **One armor slot, not per-body-part equipment.** `Player` has a single
  `EquippedArmor`. Splitting into head/chest/legs multiplies the equip
  logic without teaching anything the single-slot version doesn't already
  cover (replace-and-return-to-bag).

## What I'd add next

- **A second, non-guaranteed loot table entry actually exercised in a
  real fight**, not just in `CombatEngineTests.RollLoot`-level unit tests --
  Giant Spider's 80% Spider Silk drop is wired up but the test suite never
  plays a full spider fight end to end the way it does for rats.
- **More than one weapon tier reachable through the vendor**, so `buy` +
  `equip` changes actual combat math observably (`Wooden Sword` is
  purchasable now; `Steel Sword` only ever arrives as a quest reward).
