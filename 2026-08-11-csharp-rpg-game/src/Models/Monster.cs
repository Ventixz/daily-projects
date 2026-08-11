namespace RpgGame.Models;

public sealed record LootEntry(Item Item, int DropChancePercent, bool IsGuaranteed);

public sealed class Monster
{
    public string Name { get; }
    public int MaximumHitPoints { get; }
    public int CurrentHitPoints { get; set; }
    public int MinDamage { get; }
    public int MaxDamage { get; }
    public int RewardExperiencePoints { get; }
    public int RewardGold { get; }
    public IReadOnlyList<LootEntry> LootTable { get; }

    public Monster(
        string name,
        int maximumHitPoints,
        int minDamage,
        int maxDamage,
        int rewardExperiencePoints,
        int rewardGold,
        IReadOnlyList<LootEntry> lootTable)
    {
        Name = name;
        MaximumHitPoints = maximumHitPoints;
        CurrentHitPoints = maximumHitPoints;
        MinDamage = minDamage;
        MaxDamage = maxDamage;
        RewardExperiencePoints = rewardExperiencePoints;
        RewardGold = rewardGold;
        LootTable = lootTable;
    }

    public bool IsAlive => CurrentHitPoints > 0;
}

// A Location holds one of these, not a Monster directly. Monster carries
// mutable combat state (CurrentHitPoints); if a Location handed out the
// same Monster instance to every encounter, killing it once would leave
// every later visitor fighting a corpse with 0 HP. Spawn() hands back a
// fresh instance per encounter instead.
public sealed class MonsterTemplate
{
    private readonly Func<Monster> _factory;

    public string Name { get; }

    public MonsterTemplate(string name, Func<Monster> factory)
    {
        Name = name;
        _factory = factory;
    }

    public Monster Spawn() => _factory();
}
