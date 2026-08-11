using RpgGame;
using RpgGame.Combat;
using RpgGame.Models;
using RpgGame.World;

var world = new GameWorld();
var player = new Player("Hero", GameWorld.HomeLocationId, startingGold: 15);
var game = new Game(player, world, new CombatEngine(new SystemRandomSource()));

Console.WriteLine("A Simple RPG -- type 'help' for commands, 'quit' to exit.");
Console.WriteLine(game.ProcessCommand("look"));

while (true)
{
    Console.Write("\n> ");
    var line = Console.ReadLine();
    if (line is null) break;

    var trimmed = line.Trim();
    if (trimmed.Equals("quit", StringComparison.OrdinalIgnoreCase) ||
        trimmed.Equals("exit", StringComparison.OrdinalIgnoreCase))
    {
        Console.WriteLine("Goodbye.");
        break;
    }

    Console.WriteLine(game.ProcessCommand(line));
}
