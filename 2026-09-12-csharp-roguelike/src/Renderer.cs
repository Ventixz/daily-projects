namespace RogueGame;

/// <summary>
/// Draws a <see cref="GameEngine"/>'s state to the console: visible cells in full color,
/// explored-but-not-visible cells dimmed, unexplored cells blank.
/// </summary>
public static class Renderer
{
    public static void Draw(GameEngine engine)
    {
        if (!Console.IsOutputRedirected)
        {
            try { Console.Clear(); } catch (IOException) { /* not a real terminal */ }
        }

        var map = engine.Dungeon.Map;
        var monstersByCell = engine.Monsters
            .Where(m => m.IsAlive)
            .ToDictionary(m => (m.X, m.Y));

        for (int y = 0; y < map.Height; y++)
        {
            for (int x = 0; x < map.Width; x++)
            {
                var cell = map.GetCell(x, y);
                char glyph;
                ConsoleColor color;

                if (x == engine.Player.X && y == engine.Player.Y)
                {
                    glyph = engine.Player.Symbol;
                    color = engine.Player.Color;
                }
                else if (cell.IsInFov && monstersByCell.TryGetValue((x, y), out var monster))
                {
                    glyph = monster.Symbol;
                    color = monster.Color;
                }
                else if (cell.IsInFov)
                {
                    glyph = cell.IsWalkable ? '.' : '#';
                    color = cell.IsWalkable ? ConsoleColor.Gray : ConsoleColor.DarkYellow;
                }
                else if (cell.IsExplored)
                {
                    glyph = cell.IsWalkable ? '.' : '#';
                    color = ConsoleColor.DarkGray;
                }
                else
                {
                    glyph = ' ';
                    color = ConsoleColor.Black;
                }

                Console.ForegroundColor = color;
                Console.Write(glyph);
            }
            Console.ResetColor();
            Console.WriteLine();
        }

        Console.WriteLine();
        Console.WriteLine($"HP: {engine.Player.Health}/{engine.Player.MaxHealth}    Turn: {engine.TurnCount}    " +
                           $"Monsters left: {engine.Monsters.Count(m => m.IsAlive)}");
        foreach (var line in engine.Log)
        {
            Console.WriteLine(line);
        }

        if (engine.GameOver)
        {
            Console.WriteLine();
            Console.WriteLine(engine.Victory ? "*** VICTORY ***" : "*** GAME OVER ***");
        }
    }
}
