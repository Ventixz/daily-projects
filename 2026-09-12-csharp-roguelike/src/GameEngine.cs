using RogueSharp;
using RogueSharp.Random;
using RoguePath = RogueSharp.Path;

namespace RogueGame;

/// <summary>
/// Turn-based rules engine: move-or-attack, monster AI, win/lose checks.
/// Knows nothing about the console — <see cref="Renderer"/> reads its public state to draw.
/// </summary>
public class GameEngine
{
    private const int LogCapacity = 6;

    private readonly List<string> _log = new();
    private readonly List<Monster> _monsters;
    private readonly IRandom _random;

    public DungeonMap Dungeon { get; }
    public Player Player { get; }
    public IReadOnlyList<Monster> Monsters => _monsters;
    public IReadOnlyList<string> Log => _log;
    public int TurnCount { get; private set; }
    public bool GameOver { get; private set; }
    public bool Victory { get; private set; }

    public GameEngine(DungeonMap dungeon, Player player, List<Monster> monsters, IRandom random)
    {
        Dungeon = dungeon;
        Player = player;
        _monsters = monsters;
        _random = random;
        Dungeon.RecomputeFov(Player.X, Player.Y);
    }

    /// <summary>
    /// Attempts to move (or, if a live monster occupies the target cell, attack) by (dx, dy).
    /// A no-op bump into a wall does not cost a turn; anything else does, and triggers a
    /// full monster turn afterwards.
    /// </summary>
    public void Move(int dx, int dy)
    {
        if (GameOver) return;

        int targetX = Player.X + dx;
        int targetY = Player.Y + dy;
        var target = _monsters.FirstOrDefault(m => m.IsAlive && m.X == targetX && m.Y == targetY);

        if (target != null)
        {
            int damage = CombatSystem.ResolveAttack(Player, target, _random);
            AddLog($"You hit the {target.Name} for {damage}.");
            if (!target.IsAlive)
            {
                AddLog($"The {target.Name} dies.");
            }
        }
        else if (Dungeon.IsWalkable(targetX, targetY))
        {
            Player.X = targetX;
            Player.Y = targetY;
        }
        else
        {
            return; // bumped a wall - free action
        }

        TurnCount++;
        Dungeon.RecomputeFov(Player.X, Player.Y);
        RunMonsterTurns();
        CheckEndConditions();
    }

    private void RunMonsterTurns()
    {
        foreach (var monster in _monsters.Where(m => m.IsAlive))
        {
            if (Distance(monster, Player) <= 1)
            {
                int damage = CombatSystem.ResolveAttack(monster, Player, _random);
                AddLog($"The {monster.Name} hits you for {damage}.");
                continue;
            }

            // Simplification: a monster "notices" the player exactly when the player's own FOV
            // reaches the monster's cell, instead of computing a separate FOV per monster.
            if (Dungeon.Map.IsInFov(monster.X, monster.Y))
            {
                StepToward(monster, Player);
            }
        }
    }

    private void StepToward(Monster monster, Actor destination)
    {
        var pathFinder = new PathFinder(Dungeon.Map, diagonalCost: 1.41);
        var start = Dungeon.Map.GetCell(monster.X, monster.Y);
        var end = Dungeon.Map.GetCell(destination.X, destination.Y);

        RoguePath? path;
        try
        {
            path = pathFinder.TryFindShortestPath(start, end);
        }
        catch (NoMoreStepsException)
        {
            path = null;
        }

        if (path is null) return;

        ICell next;
        try
        {
            next = path.StepForward();
        }
        catch (NoMoreStepsException)
        {
            return;
        }

        bool occupied = _monsters.Any(m => m.IsAlive && m != monster && m.X == next.X && m.Y == next.Y);
        bool isPlayerCell = next.X == destination.X && next.Y == destination.Y;
        if (!occupied && !isPlayerCell)
        {
            monster.X = next.X;
            monster.Y = next.Y;
        }
    }

    private void CheckEndConditions()
    {
        if (!Player.IsAlive)
        {
            GameOver = true;
            AddLog("You have died. Game over.");
        }
        else if (_monsters.All(m => !m.IsAlive))
        {
            GameOver = true;
            Victory = true;
            AddLog("All monsters defeated. You win!");
        }
    }

    private void AddLog(string message)
    {
        _log.Add(message);
        while (_log.Count > LogCapacity)
        {
            _log.RemoveAt(0);
        }
    }

    private static int Distance(Actor a, Actor b) => Math.Max(Math.Abs(a.X - b.X), Math.Abs(a.Y - b.Y));
}
