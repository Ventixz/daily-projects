namespace RogueGame;

public abstract class Actor
{
    public required string Name { get; init; }
    public required char Symbol { get; init; }
    public required ConsoleColor Color { get; init; }
    public int X { get; set; }
    public int Y { get; set; }
    public required int MaxHealth { get; init; }
    public int Health { get; set; }
    public required string AttackDice { get; init; }
    public required int Defense { get; init; }

    public bool IsAlive => Health > 0;
}
