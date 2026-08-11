namespace RpgGame.Models;

// One entry in a quest's turn-in requirements. Some objective items are
// consumed on turn-in (e.g. "5 rat tails" -- proof of the kill, worthless
// after that); others are kept (e.g. "a working pickaxe" -- a tool the
// quest-giver wanted verified, not used up). ConsumedOnCompletion makes
// that a per-item decision instead of a blanket rule.
public sealed record QuestObjective(string ItemName, int Quantity, bool ConsumedOnCompletion);

public sealed class Quest
{
    public string Id { get; }
    public string Name { get; }
    public string Description { get; }
    public IReadOnlyList<QuestObjective> Objectives { get; }
    public int RewardExperiencePoints { get; }
    public int RewardGold { get; }
    public Item? RewardItem { get; }

    public Quest(
        string id,
        string name,
        string description,
        IReadOnlyList<QuestObjective> objectives,
        int rewardExperiencePoints,
        int rewardGold,
        Item? rewardItem = null)
    {
        Id = id;
        Name = name;
        Description = description;
        Objectives = objectives;
        RewardExperiencePoints = rewardExperiencePoints;
        RewardGold = rewardGold;
        RewardItem = rewardItem;
    }

    public bool IsSatisfiedBy(Inventory inventory) =>
        Objectives.All(o => inventory.HasItem(o.ItemName, o.Quantity));
}

// Tracks one player's progress on a quest they've picked up. The Quest
// itself is shared, immutable template data -- this is the per-player state.
public sealed class PlayerQuest
{
    public Quest Quest { get; }
    public bool IsCompleted { get; set; }

    public PlayerQuest(Quest quest)
    {
        Quest = quest;
        IsCompleted = false;
    }
}
