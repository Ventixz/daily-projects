using RpgGame.Models;

namespace RpgGame.World;

public sealed class GameWorld
{
    public const string HomeLocationId = "home";
    public const string TownSquareLocationId = "town-square";

    private readonly Dictionary<string, Location> _locations;

    public IReadOnlyDictionary<string, Location> Locations => _locations;

    public GameWorld()
    {
        _locations = BuildLocations();
    }

    public Location Get(string locationId)
    {
        if (!_locations.TryGetValue(locationId, out var location))
        {
            throw new ArgumentException($"Unknown location id: {locationId}");
        }

        return location;
    }

    private static Dictionary<string, Location> BuildLocations()
    {
        // Items. Declared once here and referenced by both vendor stock and
        // quest rewards, so "the Steel Sword you can buy" and "the Steel
        // Sword a quest gives you" are the same catalog entry, not two
        // separately-typo-able definitions.
        var woodenSword = new Weapon("Wooden Sword", "A basic training sword.", 10, 1, 4);
        var steelSword = new Weapon("Steel Sword", "Sharper and heavier than wood.", 45, 4, 9);
        var leatherVest = new Armor("Leather Vest", "Boiled leather, better than a tunic.", 15, 2);
        var leatherArmor = new Armor("Leather Armor", "A full leather cuirass.", 30, 4);
        var healthPotion = new Consumable("Health Potion", "Restores 10 HP.", 8, 10);
        var ratTail = new QuestObjectiveItem("Rat Tail", "Proof a field rat has been dealt with.", 1);
        var spiderSilk = new QuestObjectiveItem("Spider Silk", "Sticky, surprisingly strong thread.", 3);

        var ratProblem = new Quest(
            id: "rat-problem",
            name: "Rat Problem",
            description: "The farmer wants 3 Rat Tails as proof the field is clear.",
            objectives: new[] { new QuestObjective(ratTail.Name, 3, ConsumedOnCompletion: true) },
            rewardExperiencePoints: 50,
            rewardGold: 20,
            rewardItem: leatherArmor);

        var webSlinger = new Quest(
            id: "web-slinger",
            name: "Web Slinger",
            description: "Bring back 2 Spider Silk from the forest and I'll make it worth your while.",
            objectives: new[] { new QuestObjective(spiderSilk.Name, 2, ConsumedOnCompletion: true) },
            rewardExperiencePoints: 150,
            rewardGold: 50,
            rewardItem: steelSword);

        var fieldRatTemplate = new MonsterTemplate("Field Rat", () => new Monster(
            name: "Field Rat",
            maximumHitPoints: 8,
            minDamage: 1,
            maxDamage: 3,
            rewardExperiencePoints: 10,
            rewardGold: 3,
            lootTable: new[] { new LootEntry(ratTail, DropChancePercent: 100, IsGuaranteed: true) }));

        var barnOwlTemplate = new MonsterTemplate("Barn Owl", () => new Monster(
            name: "Barn Owl",
            maximumHitPoints: 14,
            minDamage: 2,
            maxDamage: 4,
            rewardExperiencePoints: 20,
            rewardGold: 8,
            lootTable: Array.Empty<LootEntry>()));

        var giantSpiderTemplate = new MonsterTemplate("Giant Spider", () => new Monster(
            name: "Giant Spider",
            maximumHitPoints: 20,
            minDamage: 3,
            maxDamage: 6,
            rewardExperiencePoints: 30,
            rewardGold: 10,
            lootTable: new[] { new LootEntry(spiderSilk, DropChancePercent: 80, IsGuaranteed: false) }));

        var home = new Location(HomeLocationId, "Home", "Your small cottage at the edge of town.");
        home.Exits["south"] = TownSquareLocationId;

        var townSquare = new Location(TownSquareLocationId, "Town Square", "The town's general store faces a dry fountain.");
        townSquare.Exits["north"] = HomeLocationId;
        townSquare.Exits["east"] = "farmers-field";
        townSquare.VendorInventory = new Item[] { woodenSword, leatherVest, healthPotion };

        var farmersField = new Location("farmers-field", "Farmer's Field", "Rows of trampled wheat. Rats everywhere.");
        farmersField.Exits["west"] = TownSquareLocationId;
        farmersField.Exits["north"] = "farmers-barn";
        farmersField.MonsterTemplate = fieldRatTemplate;
        farmersField.QuestOfferedHere = ratProblem;

        var farmersBarn = new Location("farmers-barn", "Farmer's Barn", "Empty now that the rats have been driven out. A path leads further out back.");
        farmersBarn.Exits["south"] = "farmers-field";
        farmersBarn.Exits["north"] = "spider-forest";
        farmersBarn.MonsterTemplate = barnOwlTemplate;
        farmersBarn.RequiredCompletedQuestId = ratProblem.Id;

        var spiderForest = new Location("spider-forest", "Spider Forest", "Thick webs stretch between every tree.");
        spiderForest.Exits["south"] = "farmers-barn";
        spiderForest.MonsterTemplate = giantSpiderTemplate;
        spiderForest.QuestOfferedHere = webSlinger;
        spiderForest.LocationToVisitFirstId = "farmers-barn";

        return new[] { home, townSquare, farmersField, farmersBarn, spiderForest }
            .ToDictionary(location => location.Id);
    }
}
