using RogueSharp;
using RogueSharp.Random;

namespace RogueGame;

public static class GameFactory
{
    public const int MapWidth = 60;
    public const int MapHeight = 25;

    /// <summary>
    /// Builds a complete, ready-to-play game: a generated dungeon, a player dropped on a
    /// random walkable cell, and monsters scattered on walkable cells far enough from the
    /// player that they don't start a fight before the player can see them coming.
    /// </summary>
    public static GameEngine NewGame(IRandom random, int monsterCount = 6)
    {
        var dungeon = new DungeonMap(MapWidth, MapHeight, maxRooms: 15, roomMaxSize: 9, roomMinSize: 4, random);
        var walkable = dungeon.WalkableCells;
        if (walkable.Count == 0)
        {
            throw new InvalidOperationException("Generated dungeon has no walkable cells.");
        }

        ICell playerCell = walkable[random.Next(0, walkable.Count - 1)];
        var player = new Player(playerCell.X, playerCell.Y);

        var monsters = SpawnMonsters(walkable, player, random, monsterCount);
        return new GameEngine(dungeon, player, monsters, random);
    }

    private static List<Monster> SpawnMonsters(List<ICell> walkable, Player player, IRandom random, int count)
    {
        const int minDistanceFromPlayer = 10;
        var candidates = walkable
            .Where(c => Math.Abs(c.X - player.X) + Math.Abs(c.Y - player.Y) >= minDistanceFromPlayer)
            .ToList();
        if (candidates.Count == 0)
        {
            candidates = walkable; // tiny map fallback: spawn anywhere but on the player
        }

        var monsters = new List<Monster>();
        var occupied = new HashSet<(int, int)> { (player.X, player.Y) };

        for (int i = 0; i < count && candidates.Count > 0; i++)
        {
            ICell cell;
            int attempts = 0;
            do
            {
                cell = candidates[random.Next(0, candidates.Count - 1)];
                attempts++;
            } while (occupied.Contains((cell.X, cell.Y)) && attempts < 50);

            if (occupied.Contains((cell.X, cell.Y))) continue;

            occupied.Add((cell.X, cell.Y));
            var kind = random.Next(0, 3) == 0 ? MonsterKind.Orc : MonsterKind.Rat;
            monsters.Add(Monster.Create(kind, cell.X, cell.Y));
        }

        return monsters;
    }
}
