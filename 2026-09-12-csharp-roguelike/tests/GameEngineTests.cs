using RogueSharp.Random;
using RogueGame;
using Xunit;

namespace RogueGame.Tests;

public class GameEngineTests
{
    private static GameEngine MakeEngine(Player player, List<Monster> monsters, RogueSharp.Random.IRandom random, int size = 10)
    {
        var dungeon = new DungeonMap(TestMaps.OpenRoom(size, size));
        return new GameEngine(dungeon, player, monsters, random);
    }

    [Fact]
    public void Move_IntoAWall_DoesNotMoveOrConsumeATurn()
    {
        var map = TestMaps.OpenRoom(10, 10);
        map.SetCellProperties(6, 5, false, false); // wall directly to the player's right
        var dungeon = new DungeonMap(map);
        var player = new Player(5, 5);
        var engine = new GameEngine(dungeon, player, new List<Monster>(), new DotNetRandom(1));

        engine.Move(1, 0);

        Assert.Equal((5, 5), (player.X, player.Y));
        Assert.Equal(0, engine.TurnCount);
    }

    [Fact]
    public void Move_IntoOpenFloor_MovesThePlayerAndAdvancesTheTurnCounter()
    {
        var player = new Player(5, 5);
        var engine = MakeEngine(player, new List<Monster>(), new DotNetRandom(1));

        engine.Move(1, 0);

        Assert.Equal((6, 5), (player.X, player.Y));
        Assert.Equal(1, engine.TurnCount);
    }

    [Fact]
    public void Move_IntoALiveMonster_AttacksInsteadOfMoving()
    {
        var player = new Player(5, 5);
        var monster = Monster.Create(MonsterKind.Rat, 6, 5);
        var random = new KnownSeriesRandom(new[] { 6 }); // player's "1d6+1" roll -> 6, +1 -> 7 damage
        var engine = MakeEngine(player, new List<Monster> { monster }, random);

        engine.Move(1, 0);

        Assert.Equal((5, 5), (player.X, player.Y)); // player stays put, this turn was an attack
        Assert.False(monster.IsAlive); // a rat has 6 HP, 7 damage kills it
        Assert.True(engine.GameOver);
        Assert.True(engine.Victory);
        Assert.Contains(engine.Log, line => line.Contains("dies"));
    }

    [Fact]
    public void RunMonsterTurns_AdjacentMonsterAttacksBack_AndCanKillThePlayer()
    {
        var player = new Player(5, 5) { Health = 1 };
        // Two cells away so the move below doesn't attack it directly; it becomes adjacent
        // only once the player steps onto (6, 5), and then gets its own turn.
        var monster = Monster.Create(MonsterKind.Orc, 7, 5);
        var random = new KnownSeriesRandom(new[] { 1 }); // orc's "1d6" roll -> 1; player's defense (2) floors it at 1
        var engine = MakeEngine(player, new List<Monster> { monster }, random);

        engine.Move(1, 0); // player steps from (5,5) to (6,5), now adjacent to the orc

        Assert.Equal(0, player.Health);
        Assert.False(player.IsAlive);
        Assert.True(engine.GameOver);
        Assert.False(engine.Victory);
        Assert.Contains(engine.Log, line => line.Contains("hits you"));
    }
}
