using RpgGame.Models;

namespace RpgGame.Tests;

public static class QuestTests
{
    public static void Run()
    {
        IsSatisfiedByRequiresExactQuantity();
        ConsumedObjectiveIsRemovedButKeptObjectiveIsNot();
    }

    private static Quest RatProblem() => new(
        id: "rat-problem",
        name: "Rat Problem",
        description: "",
        objectives: new[] { new QuestObjective("Rat Tail", 3, ConsumedOnCompletion: true) },
        rewardExperiencePoints: 50,
        rewardGold: 20);

    private static void IsSatisfiedByRequiresExactQuantity()
    {
        var quest = RatProblem();
        var inventory = new Inventory();
        var tail = new QuestObjectiveItem("Rat Tail", "", 1);
        inventory.AddItem(tail, 2);

        TestHelper.AssertFalse(quest.IsSatisfiedBy(inventory), "2 of 3 required tails should not satisfy the quest");

        inventory.AddItem(tail, 1);

        TestHelper.AssertTrue(quest.IsSatisfiedBy(inventory), "3 of 3 required tails should satisfy the quest");
    }

    // This exercises the same objective-consumption logic Game.TurnInQuest
    // uses, at the model layer: a quest can require two different items and
    // only take one of them away on completion.
    private static void ConsumedObjectiveIsRemovedButKeptObjectiveIsNot()
    {
        var pickaxeCheck = new QuestObjectiveItem("Sturdy Pickaxe", "", 20);
        var oreProof = new QuestObjectiveItem("Iron Ore", "", 1);
        var quest = new Quest(
            id: "prove-the-tool-works",
            name: "Prove the Tool Works",
            description: "",
            objectives: new[]
            {
                new QuestObjective(oreProof.Name, 2, ConsumedOnCompletion: true),
                new QuestObjective(pickaxeCheck.Name, 1, ConsumedOnCompletion: false),
            },
            rewardExperiencePoints: 10,
            rewardGold: 5);

        var inventory = new Inventory();
        inventory.AddItem(oreProof, 2);
        inventory.AddItem(pickaxeCheck, 1);

        foreach (var objective in quest.Objectives.Where(o => o.ConsumedOnCompletion))
        {
            inventory.RemoveItem(objective.ItemName, objective.Quantity);
        }

        TestHelper.AssertFalse(inventory.HasItem(oreProof.Name), "the consumable proof item should be gone after turn-in");
        TestHelper.AssertTrue(inventory.HasItem(pickaxeCheck.Name), "the kept tool should still be in the player's bag after turn-in");
    }
}
