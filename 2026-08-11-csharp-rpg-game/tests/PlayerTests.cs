using RpgGame.Models;

namespace RpgGame.Tests;

public static class PlayerTests
{
    public static void Run()
    {
        EquippingReplacesPreviousWeaponWithoutLosingIt();
        EquippingItemNotInInventoryThrows();
        DefenseNeverReducesDamageBelowOne();
        TakeDamageClampsAtZero();
        HealNeverExceedsMaximum();
        AddExperienceCanCrossMultipleLevelsAtOnce();
        LevelingUpFullyHealsPlayer();
        ReviveAfterDefeatHalvesGoldAndRestoresHealth();
    }

    private static Player NewPlayer() => new("Hero", "home", startingGold: 20);

    private static void EquippingReplacesPreviousWeaponWithoutLosingIt()
    {
        var player = NewPlayer();
        var dagger = new Weapon("Dagger", "", 5, 1, 2);
        var sword = new Weapon("Sword", "", 10, 3, 6);
        player.Inventory.AddItem(dagger);
        player.Inventory.AddItem(sword);

        player.EquipWeapon(dagger);
        player.EquipWeapon(sword);

        TestHelper.AssertEquals("Sword", player.EquippedWeapon?.Name, "second equip should become the active weapon");
        TestHelper.AssertTrue(player.Inventory.HasItem("Dagger"), "the previously equipped weapon must go back into the bag, not vanish");
    }

    private static void EquippingItemNotInInventoryThrows()
    {
        var player = NewPlayer();
        var sword = new Weapon("Sword", "", 10, 3, 6);

        var threw = false;
        try
        {
            player.EquipWeapon(sword);
        }
        catch (InvalidOperationException)
        {
            threw = true;
        }

        TestHelper.AssertTrue(threw, "equipping an item the player doesn't hold should fail loudly, not silently equip it");
    }

    private static void DefenseNeverReducesDamageBelowOne()
    {
        var player = NewPlayer();
        var thickArmor = new Armor("Fortress Plate", "", 50, 999);
        player.Inventory.AddItem(thickArmor);
        player.EquipArmor(thickArmor);

        player.TakeDamage(5);

        TestHelper.AssertEquals(19, player.CurrentHitPoints, "even absurd armor should still let 1 damage through, not make the player unhittable");
    }

    private static void TakeDamageClampsAtZero()
    {
        var player = NewPlayer();

        player.TakeDamage(9999);

        TestHelper.AssertEquals(0, player.CurrentHitPoints, "HP should clamp at 0, not go negative");
        TestHelper.AssertFalse(player.IsAlive, "0 HP should mean not alive");
    }

    private static void HealNeverExceedsMaximum()
    {
        var player = NewPlayer();
        player.TakeDamage(5);

        player.Heal(9999);

        TestHelper.AssertEquals(player.MaximumHitPoints, player.CurrentHitPoints, "healing should clamp at max HP");
    }

    private static void AddExperienceCanCrossMultipleLevelsAtOnce()
    {
        var player = NewPlayer();

        var levelsGained = player.AddExperience(300);

        TestHelper.AssertEquals(3, player.Level, "300 XP should cross both the 100 and 250 thresholds, landing on level 3");
        TestHelper.AssertEquals(2, levelsGained.Count, "a single award crossing two thresholds should report two level-ups");
    }

    private static void LevelingUpFullyHealsPlayer()
    {
        var player = NewPlayer();
        player.TakeDamage(15);

        player.AddExperience(100);

        TestHelper.AssertEquals(player.MaximumHitPoints, player.CurrentHitPoints, "leveling up should heal the player to full, matching genre convention");
    }

    private static void ReviveAfterDefeatHalvesGoldAndRestoresHealth()
    {
        var player = NewPlayer();
        player.TakeDamage(9999);

        player.ReviveAfterDefeat("home");

        TestHelper.AssertEquals(10, player.Gold, "defeat should cost exactly half of the player's gold, rounded down");
        TestHelper.AssertEquals(player.MaximumHitPoints, player.CurrentHitPoints, "reviving should restore full health");
        TestHelper.AssertEquals("home", player.CurrentLocationId, "reviving should send the player back to the given location");
        TestHelper.AssertTrue(player.IsAlive, "a revived player must be alive again");
    }
}
