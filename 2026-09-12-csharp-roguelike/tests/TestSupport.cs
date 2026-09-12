using System.Diagnostics.CodeAnalysis;
using RogueSharp;
using RogueGame;

namespace RogueGame.Tests;

/// <summary>A bare-bones <see cref="Actor"/> so combat tests can pick exact stats instead of Player/Monster's fixed ones.</summary>
internal sealed class TestActor : Actor
{
    [SetsRequiredMembers]
    public TestActor(string name, string attackDice, int defense, int maxHealth)
    {
        Name = name;
        Symbol = '?';
        Color = ConsoleColor.White;
        AttackDice = attackDice;
        Defense = defense;
        MaxHealth = maxHealth;
        Health = maxHealth;
    }
}

internal static class TestMaps
{
    /// <summary>A fully open, fully transparent square room - no random generation, so engine
    /// tests can place actors at exact coordinates without worrying about wall placement.</summary>
    public static Map OpenRoom(int width, int height)
    {
        var map = new Map(width, height);
        for (int x = 0; x < width; x++)
        {
            for (int y = 0; y < height; y++)
            {
                map.SetCellProperties(x, y, true, true);
            }
        }
        return map;
    }
}
