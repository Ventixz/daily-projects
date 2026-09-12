using RogueSharp;
using RogueSharp.MapCreation;
using RogueSharp.Random;

namespace RogueGame;

/// <summary>
/// Thin wrapper around a RogueSharp <see cref="Map"/>: room-and-corridor generation,
/// field-of-view, and the walkable-cell queries the game and its AI need.
/// </summary>
public class DungeonMap
{
    public const int FovRadius = 8;

    public Map Map { get; }

    public DungeonMap(int width, int height, int maxRooms, int roomMaxSize, int roomMinSize, IRandom random)
    {
        var strategy = new RandomRoomsMapCreationStrategy<Map>(
            width, height, maxRooms, roomMaxSize, roomMinSize, random);
        Map = Map.Create(strategy);
    }

    /// <summary>Wraps an already-built map directly - mainly so tests can hand it a small, fully-open room.</summary>
    public DungeonMap(Map map)
    {
        Map = map;
    }

    public List<ICell> WalkableCells => Map.GetAllCells().Where(c => c.IsWalkable).ToList();

    public bool IsWalkable(int x, int y) => Map.IsWalkable(x, y);

    /// <summary>
    /// Recomputes which cells are currently visible from (aroundX, aroundY).
    /// RogueSharp's <see cref="Map.ComputeFov"/> updates <c>IsInFov</c> but, as of 4.2.0,
    /// does not itself flip <c>IsExplored</c> - so newly-visible cells are marked explored
    /// here, which is what lets a cell stay dimly drawn (fog of war) after it leaves FOV.
    /// </summary>
    public void RecomputeFov(int aroundX, int aroundY)
    {
        var visible = Map.ComputeFov(aroundX, aroundY, FovRadius, true);
        foreach (var cell in visible)
        {
            Map.SetCellProperties(cell.X, cell.Y, cell.IsTransparent, cell.IsWalkable, true);
        }
    }
}
