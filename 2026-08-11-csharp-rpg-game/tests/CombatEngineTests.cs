using RpgGame.Combat;
using RpgGame.Models;

namespace RpgGame.Tests;

public static class CombatEngineTests
{
    public static void Run()
    {
        KillingBlowGivesMonsterNoRetaliation();
        NonLethalRoundDamagesBothSides();
        SpawnedMonstersAreIndependentInstances();
        GuaranteedLootAlwaysDropsWithoutConsumingARoll();
        ChanceLootRespectsTheRoll();
        FreeAttackOnFleeCanKillThePlayer();
    }

    private static Monster NewRat() => new(
        name: "Field Rat",
        maximumHitPoints: 8,
        minDamage: 1,
        maxDamage: 3,
        rewardExperiencePoints: 10,
        rewardGold: 3,
        lootTable: Array.Empty<LootEntry>());

    private static void KillingBlowGivesMonsterNoRetaliation()
    {
        var player = new Player("Hero", "home");
        var monster = NewRat(); // 8 HP
        var engine = new CombatEngine(new FakeRandomSource(8)); // one hit, exactly lethal

        var result = engine.ExecuteRound(player, monster);

        TestHelper.AssertTrue(result.MonsterDefeated, "an 8-damage hit on an 8-HP monster should be lethal");
        TestHelper.AssertEquals<int?>(null, result.MonsterDamageDealt, "a monster killed by the player's own swing should not get to hit back");
        TestHelper.AssertEquals(20, player.CurrentHitPoints, "player should take no damage on a round that kills the monster");
    }

    private static void NonLethalRoundDamagesBothSides()
    {
        var player = new Player("Hero", "home");
        var monster = NewRat();
        var engine = new CombatEngine(new FakeRandomSource(2, 3)); // player hits for 2, monster hits for 3

        var result = engine.ExecuteRound(player, monster);

        TestHelper.AssertEquals(6, monster.CurrentHitPoints, "monster should take the player's damage");
        TestHelper.AssertEquals(3, result.MonsterDamageDealt, "monster's counter damage should be reported");
        TestHelper.AssertEquals(17, player.CurrentHitPoints, "player should take the monster's counter damage (no armor equipped)");
        TestHelper.AssertFalse(result.MonsterDefeated, "6 remaining HP is not defeated");
        TestHelper.AssertFalse(result.PlayerDefeated, "17 remaining HP is not defeated");
    }

    private static void SpawnedMonstersAreIndependentInstances()
    {
        var template = new MonsterTemplate("Field Rat", NewRat);

        var first = template.Spawn();
        first.CurrentHitPoints = 0;
        var second = template.Spawn();

        TestHelper.AssertEquals(8, second.CurrentHitPoints, "a fresh spawn must not inherit the previous encounter's damage");
        TestHelper.AssertTrue(second.IsAlive, "a newly spawned monster should be alive regardless of prior kills from the same template");
    }

    private static void GuaranteedLootAlwaysDropsWithoutConsumingARoll()
    {
        var tail = new QuestObjectiveItem("Rat Tail", "", 1);
        var silk = new QuestObjectiveItem("Spider Silk", "", 3);
        var monster = new Monster("Field Rat", 8, 1, 3, 10, 3, new[]
        {
            new LootEntry(tail, DropChancePercent: 100, IsGuaranteed: true),
            new LootEntry(silk, DropChancePercent: 50, IsGuaranteed: false),
        });
        // Only one queued value: the guaranteed entry must not consume a roll,
        // so this single value is left for the 50%-chance entry's check.
        var engine = new CombatEngine(new FakeRandomSource(10));

        var loot = engine.RollLoot(monster);

        TestHelper.AssertEquals(2, loot.Count, "guaranteed entry plus a roll of 10 (<=50) should both drop");
        TestHelper.AssertTrue(loot.Contains(tail), "guaranteed item should always be present");
        TestHelper.AssertTrue(loot.Contains(silk), "a roll under the drop chance should include the chance item");
    }

    private static void ChanceLootRespectsTheRoll()
    {
        var silk = new QuestObjectiveItem("Spider Silk", "", 3);
        var monster = new Monster("Giant Spider", 20, 3, 6, 30, 10, new[]
        {
            new LootEntry(silk, DropChancePercent: 50, IsGuaranteed: false),
        });
        var engine = new CombatEngine(new FakeRandomSource(51)); // just over the 50% threshold

        var loot = engine.RollLoot(monster);

        TestHelper.AssertEquals(0, loot.Count, "a roll above the drop chance should yield no loot");
    }

    private static void FreeAttackOnFleeCanKillThePlayer()
    {
        var player = new Player("Hero", "home");
        player.TakeDamage(18); // down to 2 HP
        var monster = NewRat();
        var engine = new CombatEngine(new FakeRandomSource(3));

        var damage = engine.MonsterFreeAttack(player, monster);

        TestHelper.AssertEquals(3, damage, "the free attack's damage should be reported to the caller");
        TestHelper.AssertFalse(player.IsAlive, "fleeing is a real risk -- the parting shot can be lethal");
    }
}
