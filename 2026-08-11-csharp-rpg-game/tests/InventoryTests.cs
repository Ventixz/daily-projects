using RpgGame.Models;

namespace RpgGame.Tests;

public static class InventoryTests
{
    public static void Run()
    {
        AddingSameItemTwiceStacksQuantity();
        RemovingPartialQuantityKeepsSlot();
        RemovingFullQuantityDropsSlot();
        RemovingMoreThanHeldFailsAndChangesNothing();
        HasItemRespectsRequestedQuantity();
    }

    private static void AddingSameItemTwiceStacksQuantity()
    {
        var inventory = new Inventory();
        var potion = new Consumable("Health Potion", "", 8, 10);

        inventory.AddItem(potion, 2);
        inventory.AddItem(potion, 3);

        TestHelper.AssertEquals(1, inventory.Slots.Count, "stacking should not create a second slot");
        TestHelper.AssertEquals(5, inventory.Slots[0].Quantity, "quantities should sum across AddItem calls");
    }

    private static void RemovingPartialQuantityKeepsSlot()
    {
        var inventory = new Inventory();
        var tail = new QuestObjectiveItem("Rat Tail", "", 1);
        inventory.AddItem(tail, 3);

        var removed = inventory.RemoveItem(tail.Name, 1);

        TestHelper.AssertTrue(removed, "removing 1 of 3 should succeed");
        TestHelper.AssertEquals(2, inventory.Slots[0].Quantity, "slot should still hold the remainder");
    }

    private static void RemovingFullQuantityDropsSlot()
    {
        var inventory = new Inventory();
        var tail = new QuestObjectiveItem("Rat Tail", "", 1);
        inventory.AddItem(tail, 2);

        inventory.RemoveItem(tail.Name, 2);

        TestHelper.AssertEquals(0, inventory.Slots.Count, "emptying a slot should remove it, not leave a 0-quantity entry");
        TestHelper.AssertFalse(inventory.HasItem(tail.Name), "an item with 0 remaining should not be reported as held");
    }

    private static void RemovingMoreThanHeldFailsAndChangesNothing()
    {
        var inventory = new Inventory();
        var tail = new QuestObjectiveItem("Rat Tail", "", 1);
        inventory.AddItem(tail, 2);

        var removed = inventory.RemoveItem(tail.Name, 5);

        TestHelper.AssertFalse(removed, "removing more than held should report failure");
        TestHelper.AssertEquals(2, inventory.Slots[0].Quantity, "a failed removal must not partially consume the stack");
    }

    private static void HasItemRespectsRequestedQuantity()
    {
        var inventory = new Inventory();
        var tail = new QuestObjectiveItem("Rat Tail", "", 1);
        inventory.AddItem(tail, 2);

        TestHelper.AssertTrue(inventory.HasItem(tail.Name, 2), "holding exactly the requested quantity should count");
        TestHelper.AssertFalse(inventory.HasItem(tail.Name, 3), "holding fewer than requested should not count");
    }
}
