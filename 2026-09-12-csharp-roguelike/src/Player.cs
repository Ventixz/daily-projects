using System.Diagnostics.CodeAnalysis;

namespace RogueGame;

public class Player : Actor
{
    [SetsRequiredMembers]
    public Player(int x, int y)
    {
        Name = "you";
        Symbol = '@';
        Color = ConsoleColor.Yellow;
        X = x;
        Y = y;
        MaxHealth = 20;
        Health = MaxHealth;
        AttackDice = "1d6+1";
        Defense = 2;
    }
}
