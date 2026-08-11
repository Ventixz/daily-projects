using RpgGame.Combat;
using RpgGame.Models;
using RpgGame.World;

namespace RpgGame.Tests;

// Exercises Game.ProcessCommand end to end -- the same entry point
// Program.cs's Console loop calls -- with no real terminal involved. This
// is the payoff of keeping Game free of Console.Read/Write: a full played
// session is just a list of strings in, and assertions on the resulting
// Player state.
public static class GameFlowTests
{
    public static void Run()
    {
        QuestLifecycleGrindsRatsTurnsInAndUnlocksBarn();
        VendorPurchaseAndConsumableRoundTrip();
    }

    private static void QuestLifecycleGrindsRatsTurnsInAndUnlocksBarn()
    {
        // Bare-handed damage is 1-2. Each rat has 8 HP: four hits of 2 kill
        // it cleanly on the last swing, so only 3 retaliation rolls are
        // needed per rat (the killing round gets no counter-hit). The rat's
        // Rat Tail drop is guaranteed, so no loot roll is queued for it.
        // Retaliation is kept at 1 damage/hit (3 per rat, 9 across all
        // three) so the player -- starting at 20 HP with no armor yet --
        // survives all three fights; this caught a real bug during writing,
        // where 3 damage/hit put the player at 2 HP after two rats and the
        // third rat's first hit killed them mid-test.
        var perRatSequence = new[] { 2, 1, 2, 1, 2, 1, 2 };
        var queue = perRatSequence.Concat(perRatSequence).Concat(perRatSequence).ToArray();
        var game = NewGame(queue);

        Send(game, "move south"); // home -> town square
        Send(game, "move east");  // town square -> farmer's field (spawns a rat)
        Send(game, "accept");     // Rat Problem

        for (var ratNumber = 1; ratNumber <= 3; ratNumber++)
        {
            string result;
            do
            {
                result = game.ProcessCommand("attack");
            } while (!result.Contains("defeated"));

            TestHelper.AssertTrue(game.Player.Inventory.HasItem("Rat Tail", ratNumber), $"should hold {ratNumber} Rat Tail(s) after killing rat #{ratNumber}");

            if (ratNumber < 3)
            {
                Send(game, "move west"); // back to town square
                Send(game, "move east"); // re-enter field, spawns a fresh rat
            }
        }

        TestHelper.AssertFalse(game.Player.Inventory.HasItem("Rat Tail", 4), "should not somehow hold more tails than rats killed");

        var barnBeforeTurnIn = Send(game, "move north");
        TestHelper.AssertTrue(barnBeforeTurnIn.Contains("can't get through"), "barn should still be locked before the quest is turned in");

        var turnIn = Send(game, "turnin");
        TestHelper.AssertTrue(turnIn.Contains("Quest complete"), "turning in with 3 tails held should succeed");
        TestHelper.AssertTrue(game.Player.Inventory.HasItem("Leather Armor"), "quest reward item should land in the inventory");
        TestHelper.AssertFalse(game.Player.Inventory.HasItem("Rat Tail"), "turn-in should consume the tails since the objective is marked ConsumedOnCompletion");

        var barnAfterTurnIn = Send(game, "move north");
        TestHelper.AssertTrue(barnAfterTurnIn.Contains("Farmer's Barn"), "barn should be reachable once the quest is completed");
        TestHelper.AssertEquals("farmers-barn", game.Player.CurrentLocationId, "player should now be standing in the barn");
    }

    private static void VendorPurchaseAndConsumableRoundTrip()
    {
        // attack: player deals 1 (non-lethal against an 8-HP rat), monster deals 3.
        // flee: roll of 10 is <=50, so the escape succeeds and costs nothing further.
        var game = NewGame(new[] { 1, 3, 10 });

        Send(game, "move south"); // home -> town square
        var buy = Send(game, "buy Health Potion");
        TestHelper.AssertTrue(buy.Contains("Bought"), "buying a listed vendor item should succeed");
        TestHelper.AssertEquals(7, game.Player.Gold, "15 starting gold minus an 8g potion should leave 7g");

        Send(game, "move east"); // spawns a rat
        Send(game, "attack");    // player 17 HP now (20 - 3)
        var flee = Send(game, "flee");
        TestHelper.AssertTrue(flee.Contains("escape"), "a roll of 10 against a 50% flee chance should succeed");
        TestHelper.AssertEquals(17, game.Player.CurrentHitPoints, "a clean escape should not cost any further HP");

        var use = Send(game, "use Health Potion");
        TestHelper.AssertTrue(use.Contains("recover 10 HP"), "using the exact item bought from the vendor should apply its real HealAmount");
        TestHelper.AssertEquals(20, game.Player.CurrentHitPoints, "17 + 10 healing should clamp at the 20 HP maximum, not overshoot to 27");
        TestHelper.AssertFalse(game.Player.Inventory.HasItem("Health Potion"), "the potion should be consumed after use");
    }

    private static Game NewGame(int[] randomSequence)
    {
        var world = new GameWorld();
        var player = new Player("Hero", GameWorld.HomeLocationId, startingGold: 15);
        return new Game(player, world, new CombatEngine(new FakeRandomSource(randomSequence)));
    }

    private static string Send(Game game, string command) => game.ProcessCommand(command);
}
