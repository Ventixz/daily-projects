using RogueSharp.Random;
using RogueGame;
using Xunit;

namespace RogueGame.Tests;

public class DungeonMapTests
{
    [Fact]
    public void RecomputeFov_MarksNewlyVisibleCellsAsExplored()
    {
        // Regression test: RogueSharp 4.2.0's Map.ComputeFov flips IsInFov but leaves
        // IsExplored untouched. DungeonMap.RecomputeFov has to do that bookkeeping itself,
        // or "fog of war" cells would flicker back to blank the instant they left view.
        var map = TestMaps.OpenRoom(10, 10);
        var dungeon = new DungeonMap(map);

        Assert.False(dungeon.Map.IsExplored(5, 5));

        dungeon.RecomputeFov(5, 5);

        Assert.True(dungeon.Map.IsInFov(5, 5));
        Assert.True(dungeon.Map.IsExplored(5, 5));
    }

    [Fact]
    public void RecomputeFov_LeavesCellsOutsideRadiusUnexplored()
    {
        var map = TestMaps.OpenRoom(40, 40);
        var dungeon = new DungeonMap(map);

        dungeon.RecomputeFov(0, 0);

        Assert.True(dungeon.Map.IsExplored(0, 0));
        Assert.False(dungeon.Map.IsExplored(39, 39));
    }

    [Fact]
    public void GeneratedDungeon_HasRequestedDimensionsAndAtLeastOneWalkableCell()
    {
        var random = new DotNetRandom(1234);
        var dungeon = new DungeonMap(width: 60, height: 25, maxRooms: 15, roomMaxSize: 9, roomMinSize: 4, random);

        Assert.Equal(60, dungeon.Map.Width);
        Assert.Equal(25, dungeon.Map.Height);
        Assert.NotEmpty(dungeon.WalkableCells);
    }
}
