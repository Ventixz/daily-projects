using RpgGame.Models;
using RpgGame.World;

namespace RpgGame.Tests;

public static class WorldTests
{
    public static void Run()
    {
        VisitGateBlocksUntilPrerequisiteLocationVisited();
        QuestGateBlocksUntilQuestCompleted();
        RealWorldGraphIsFullyConnectedAndConsistent();
    }

    private static void VisitGateBlocksUntilPrerequisiteLocationVisited()
    {
        var gated = new Location("deep-woods", "Deep Woods", "");
        gated.LocationToVisitFirstId = "clearing";
        var player = new Player("Hero", "town");

        TestHelper.AssertFalse(gated.IsAccessibleTo(player), "should be blocked before visiting the prerequisite location");

        player.VisitedLocationIds.Add("clearing");

        TestHelper.AssertTrue(gated.IsAccessibleTo(player), "should open up once the prerequisite has been visited");
    }

    private static void QuestGateBlocksUntilQuestCompleted()
    {
        var gated = new Location("treasure-room", "Treasure Room", "");
        gated.RequiredCompletedQuestId = "rat-problem";
        var player = new Player("Hero", "town");
        var quest = new Quest("rat-problem", "Rat Problem", "", Array.Empty<QuestObjective>(), 0, 0);

        TestHelper.AssertFalse(gated.IsAccessibleTo(player), "should be blocked with no quest accepted at all");

        var playerQuest = new PlayerQuest(quest);
        player.Quests.Add(playerQuest);
        TestHelper.AssertFalse(gated.IsAccessibleTo(player), "should still be blocked while the quest is merely in progress");

        playerQuest.IsCompleted = true;
        TestHelper.AssertTrue(gated.IsAccessibleTo(player), "should open once the quest is marked completed");
    }

    // Every exit has to point somewhere real, or a player who walks in that
    // direction hits a crash instead of a location. This test would have
    // caught a typo'd location id in GameWorld's hand-written exit table
    // immediately instead of leaving it for someone to discover by playing.
    private static void RealWorldGraphIsFullyConnectedAndConsistent()
    {
        var world = new GameWorld();

        foreach (var location in world.Locations.Values)
        {
            foreach (var (direction, destinationId) in location.Exits)
            {
                var destinationExists = world.Locations.ContainsKey(destinationId);
                TestHelper.AssertTrue(destinationExists, $"{location.Id}'s '{direction}' exit points to unknown location '{destinationId}'");
            }
        }

        TestHelper.AssertTrue(world.Locations.ContainsKey(GameWorld.HomeLocationId), "world must contain the declared home location id");
        TestHelper.AssertTrue(world.Locations.ContainsKey(GameWorld.TownSquareLocationId), "world must contain the declared town square location id");
    }
}
