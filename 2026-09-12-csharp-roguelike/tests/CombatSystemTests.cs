using RogueSharp.Random;
using RogueGame;
using Xunit;

namespace RogueGame.Tests;

public class CombatSystemTests
{
    [Fact]
    public void ResolveAttack_SubtractsDefenseFromRoll()
    {
        var attacker = new TestActor("goblin", "1d6", defense: 0, maxHealth: 10);
        var defender = new TestActor("dummy", "1d1", defense: 1, maxHealth: 10);
        var random = new KnownSeriesRandom(new[] { 4 }); // the "1d6" roll comes back as 4

        int damage = CombatSystem.ResolveAttack(attacker, defender, random);

        Assert.Equal(3, damage); // 4 rolled - 1 defense
        Assert.Equal(7, defender.Health);
    }

    [Fact]
    public void ResolveAttack_FloorsDamageAtOne_EvenWhenDefenseExceedsTheRoll()
    {
        var attacker = new TestActor("weakling", "1d4", defense: 0, maxHealth: 10);
        var defender = new TestActor("tank", "1d1", defense: 10, maxHealth: 10);
        var random = new KnownSeriesRandom(new[] { 1 });

        int damage = CombatSystem.ResolveAttack(attacker, defender, random);

        Assert.Equal(1, damage);
        Assert.Equal(9, defender.Health);
    }

    [Fact]
    public void ResolveAttack_ClampsHealthAtZero_NeverGoesNegative()
    {
        var attacker = new TestActor("ogre", "1d6", defense: 0, maxHealth: 10);
        var defender = new TestActor("frail", "1d1", defense: 0, maxHealth: 2);
        var random = new KnownSeriesRandom(new[] { 6 });

        CombatSystem.ResolveAttack(attacker, defender, random);

        Assert.Equal(0, defender.Health);
        Assert.False(defender.IsAlive);
    }
}
