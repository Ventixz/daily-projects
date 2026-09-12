using RogueGame;
using RogueSharp;
using RogueSharp.Random;
using RoguePath = RogueSharp.Path;

int? seed = ParseIntArg(args, "--seed");
bool demo = args.Contains("--demo") || Console.IsInputRedirected;

IRandom random = seed.HasValue ? new DotNetRandom(seed.Value) : new DotNetRandom();
GameEngine engine = GameFactory.NewGame(random);

if (demo)
{
    RunDemo(engine, seed);
}
else
{
    RunInteractive(engine);
}

static int? ParseIntArg(string[] args, string name)
{
    for (int i = 0; i < args.Length - 1; i++)
    {
        if (args[i] == name && int.TryParse(args[i + 1], out int value))
        {
            return value;
        }
    }
    return null;
}

static void RunInteractive(GameEngine engine)
{
    Console.CursorVisible = false;
    Renderer.Draw(engine);

    while (!engine.GameOver)
    {
        var key = Console.ReadKey(intercept: true).Key;
        (int dx, int dy)? move = key switch
        {
            ConsoleKey.UpArrow or ConsoleKey.W or ConsoleKey.K => (0, -1),
            ConsoleKey.DownArrow or ConsoleKey.S or ConsoleKey.J => (0, 1),
            ConsoleKey.LeftArrow or ConsoleKey.A or ConsoleKey.H => (-1, 0),
            ConsoleKey.RightArrow or ConsoleKey.D or ConsoleKey.L => (1, 0),
            ConsoleKey.Q => null,
            _ => (0, 0),
        };

        if (key == ConsoleKey.Q) break;
        if (move is { } m) engine.Move(m.dx, m.dy);
        Renderer.Draw(engine);
    }

    Console.CursorVisible = true;
}

/// <summary>
/// Non-interactive smoke test / demo: with a fixed seed, walk the player through a scripted
/// sequence of moves (deterministic, since the dungeon, monster placement, and combat all
/// draw from the same seeded random) and print the final state. Used by `dotnet test` via
/// GameFactoryTests, and directly with `dotnet run -- --demo --seed 42` to watch a full
/// playthrough scroll by without a terminal.
/// </summary>
static void RunDemo(GameEngine engine, int? seed)
{
    Console.WriteLine($"[demo mode] seed={seed?.ToString() ?? "(none, non-deterministic)"}");
    Renderer.Draw(engine);

    // A short, greedy exploration script: repeatedly step toward the nearest cell that
    // isn't explored yet, which in practice walks the player through corridors, into rooms,
    // and into monsters along the way. Caps at 200 turns so a bad seed can't hang forever.
    const int maxTurns = 200;
    while (!engine.GameOver && engine.TurnCount < maxTurns)
    {
        var (dx, dy) = GreedyStepTowardUnexplored(engine);
        if (dx == 0 && dy == 0) break; // nothing left reachable to explore
        engine.Move(dx, dy);
    }

    Renderer.Draw(engine);
    Console.WriteLine();
    Console.WriteLine($"[demo mode] finished after {engine.TurnCount} turns, " +
                       $"game over = {engine.GameOver}, victory = {engine.Victory}");
}

/// <summary>
/// Picks the nearest not-yet-explored walkable cell and returns the first step of the
/// shortest path to it, using the same <see cref="PathFinder"/> the monster AI uses.
/// Tries progressively farther candidates in case the nearest ones sit in a disconnected
/// pocket of the map (a valid, if rare, outcome of room-and-corridor generation).
/// </summary>
static (int dx, int dy) GreedyStepTowardUnexplored(GameEngine engine)
{
    var map = engine.Dungeon.Map;
    var player = engine.Player;
    var start = map.GetCell(player.X, player.Y);
    var pathFinder = new PathFinder(map, diagonalCost: 1.41);

    var candidates = map.GetAllCells()
        .Where(c => c.IsWalkable && !c.IsExplored)
        .OrderBy(c => Math.Abs(c.X - player.X) + Math.Abs(c.Y - player.Y))
        .Take(10);

    foreach (var target in candidates)
    {
        RoguePath? path;
        try
        {
            path = pathFinder.TryFindShortestPath(start, target);
        }
        catch (NoMoreStepsException)
        {
            path = null;
        }
        if (path is null) continue;

        try
        {
            var next = path.StepForward();
            return (next.X - player.X, next.Y - player.Y);
        }
        catch (NoMoreStepsException)
        {
            continue;
        }
    }

    return (0, 0);
}
