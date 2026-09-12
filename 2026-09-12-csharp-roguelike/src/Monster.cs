using System.Diagnostics.CodeAnalysis;

namespace RogueGame;

public enum MonsterKind
{
    Rat,
    Orc,
}

public class Monster : Actor
{
    public MonsterKind Kind { get; }

    [SetsRequiredMembers]
    private Monster(MonsterKind kind, int x, int y, string name, char symbol, ConsoleColor color,
        int maxHealth, string attackDice, int defense)
    {
        Kind = kind;
        X = x;
        Y = y;
        Name = name;
        Symbol = symbol;
        Color = color;
        MaxHealth = maxHealth;
        Health = maxHealth;
        AttackDice = attackDice;
        Defense = defense;
    }

    public static Monster Create(MonsterKind kind, int x, int y) => kind switch
    {
        MonsterKind.Rat => new Monster(kind, x, y, "rat", 'r', ConsoleColor.DarkGray,
            maxHealth: 6, attackDice: "1d3", defense: 0),
        MonsterKind.Orc => new Monster(kind, x, y, "orc", 'o', ConsoleColor.Green,
            maxHealth: 12, attackDice: "1d6", defense: 1),
        _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, null),
    };
}
