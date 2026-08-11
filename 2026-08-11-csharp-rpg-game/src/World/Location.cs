using RpgGame.Models;

namespace RpgGame.World;

public sealed class Location
{
    public string Id { get; }
    public string Name { get; }
    public string Description { get; }

    // direction ("north", "south", ...) -> destination location id.
    public Dictionary<string, string> Exits { get; } = new();

    public MonsterTemplate? MonsterTemplate { get; set; }
    public Quest? QuestOfferedHere { get; set; }
    public IReadOnlyList<Item> VendorInventory { get; set; } = Array.Empty<Item>();

    // If set, this location refuses entry until the player has already
    // visited LocationToVisitFirstId at least once -- e.g. you have to pass
    // through the field before the path behind it exists on your map.
    public string? LocationToVisitFirstId { get; set; }

    // A stronger gate than LocationToVisitFirstId: refuses entry until a
    // specific quest is completed, not just visited. Used for content that's
    // meant to be a reward for finishing a questline, not just wandering in.
    public string? RequiredCompletedQuestId { get; set; }

    public Location(string id, string name, string description)
    {
        Id = id;
        Name = name;
        Description = description;
    }

    public bool IsAccessibleTo(Player player)
    {
        if (LocationToVisitFirstId is not null && !player.VisitedLocationIds.Contains(LocationToVisitFirstId))
        {
            return false;
        }

        if (RequiredCompletedQuestId is not null && player.FindQuest(RequiredCompletedQuestId)?.IsCompleted != true)
        {
            return false;
        }

        return true;
    }
}
