using RogueSharp.Random;
using RogueGame;
using Xunit;

namespace RogueGame.Tests;

public class GameFactoryTests
{
    [Fact]
    public void NewGame_PlacesPlayerOnAWalkableCell()
    {
        var engine = GameFactory.NewGame(new DotNetRandom(7));

        Assert.True(engine.Dungeon.IsWalkable(engine.Player.X, engine.Player.Y));
    }

    [Fact]
    public void NewGame_SpawnsRequestedMonsterCount_NoneOnThePlayersCell()
    {
        var engine = GameFactory.NewGame(new DotNetRandom(7), monsterCount: 6);

        Assert.Equal(6, engine.Monsters.Count);
        Assert.DoesNotContain(engine.Monsters, m => m.X == engine.Player.X && m.Y == engine.Player.Y);
        Assert.All(engine.Monsters, m => Assert.True(engine.Dungeon.IsWalkable(m.X, m.Y)));
    }

    [Fact]
    public void NewGame_WithTheSameSeed_IsFullyReproducible()
    {
        var engineA = GameFactory.NewGame(new DotNetRandom(99), monsterCount: 6);
        var engineB = GameFactory.NewGame(new DotNetRandom(99), monsterCount: 6);

        Assert.Equal((engineA.Player.X, engineA.Player.Y), (engineB.Player.X, engineB.Player.Y));
        Assert.Equal(
            engineA.Monsters.Select(m => (m.Kind, m.X, m.Y)),
            engineB.Monsters.Select(m => (m.Kind, m.X, m.Y)));
    }
}
