using RpgGame.Combat;
using RpgGame.Models;
using RpgGame.World;

namespace RpgGame;

// All game logic lives behind ProcessCommand(string) -> string, with no
// Console.Read/Write anywhere in this class. That's what lets the test
// suite drive a full play session (move, fight, loot, turn in a quest, buy
// a potion) without a real terminal -- Program.cs is the only place that
// touches Console, and it's a thin loop around this.
public sealed class Game
{
    private readonly GameWorld _world;
    private readonly CombatEngine _combat;
    public Player Player { get; }

    // Non-null while a fight is in progress. Combat blocks movement: you
    // have to fight it out or flee, not just walk away for free.
    private Monster? _currentMonster;

    public Game(Player player, GameWorld world, CombatEngine combat)
    {
        Player = player;
        _world = world;
        _combat = combat;
    }

    public string ProcessCommand(string rawInput)
    {
        var input = rawInput.Trim();
        if (input.Length == 0) return "";

        var parts = input.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
        var verb = parts[0].ToLowerInvariant();
        var argument = parts.Length > 1 ? parts[1].Trim() : "";

        return verb switch
        {
            "look" => Look(),
            "move" or "go" => Move(argument),
            "north" or "n" => Move("north"),
            "south" or "s" => Move("south"),
            "east" or "e" => Move("east"),
            "west" or "w" => Move("west"),
            "attack" or "fight" => Attack(),
            "flee" or "run" => Flee(),
            "inventory" or "i" => Inventory(),
            "equip" => Equip(argument),
            "use" => Use(argument),
            "status" => Status(),
            "quests" => Quests(),
            "accept" => AcceptQuest(),
            "turnin" or "complete" => TurnInQuest(),
            "buy" => Buy(argument),
            "sell" => Sell(argument),
            "help" => Help(),
            _ => $"Unknown command: '{verb}'. Type 'help' for a list of commands.",
        };
    }

    private Location CurrentLocation => _world.Get(Player.CurrentLocationId);

    private string Look()
    {
        var location = CurrentLocation;
        var lines = new List<string> { $"== {location.Name} ==", location.Description };

        if (_currentMonster is { IsAlive: true } monster)
        {
            lines.Add($"A {monster.Name} blocks your way ({monster.CurrentHitPoints}/{monster.MaximumHitPoints} HP). You must attack or flee.");
            return string.Join('\n', lines);
        }

        if (location.VendorInventory.Count > 0)
        {
            lines.Add("The store sells: " + string.Join(", ", location.VendorInventory.Select(i => $"{i.Name} ({i.Value}g)")));
        }

        if (location.QuestOfferedHere is { } quest)
        {
            var playerQuest = Player.FindQuest(quest.Id);
            if (playerQuest is null)
            {
                lines.Add($"Quest available: '{quest.Name}' -- {quest.Description} (type 'accept')");
            }
            else if (!playerQuest.IsCompleted)
            {
                lines.Add($"Quest in progress: '{quest.Name}' -- turn in with 'turnin' once you have the items.");
            }
        }

        var exits = location.Exits.Keys
            .Where(direction => location.Exits.TryGetValue(direction, out var id) && _world.Get(id).IsAccessibleTo(Player))
            .ToList();
        lines.Add(exits.Count > 0 ? "Exits: " + string.Join(", ", exits) : "No visible exits.");

        return string.Join('\n', lines);
    }

    private string Move(string direction)
    {
        if (_currentMonster is { IsAlive: true })
        {
            return "You can't leave -- you're in combat. 'attack' or 'flee'.";
        }

        direction = direction.ToLowerInvariant();
        var location = CurrentLocation;
        if (!location.Exits.TryGetValue(direction, out var destinationId))
        {
            return $"There's no way to go '{direction}' from here.";
        }

        var destination = _world.Get(destinationId);
        if (!destination.IsAccessibleTo(Player))
        {
            return "You can't get through that way yet.";
        }

        Player.CurrentLocationId = destinationId;
        Player.VisitedLocationIds.Add(destinationId);

        var lines = new List<string> { $"You travel {direction} to {destination.Name}." };

        if (destination.MonsterTemplate is { } template)
        {
            _currentMonster = template.Spawn();
            lines.Add($"A {_currentMonster.Name} attacks! ({_currentMonster.CurrentHitPoints} HP)");
        }

        lines.Add(Look());
        return string.Join('\n', lines);
    }

    private string Attack()
    {
        if (_currentMonster is not { IsAlive: true } monster)
        {
            return "There's nothing here to fight.";
        }

        var result = _combat.ExecuteRound(Player, monster);
        var lines = new List<string> { $"You hit the {monster.Name} for {result.PlayerDamageDealt}." };

        if (result.MonsterDefeated)
        {
            lines.Add($"The {monster.Name} is defeated!");
            lines.Add(AwardVictory(monster));
            _currentMonster = null;
            return string.Join('\n', lines);
        }

        lines.Add($"The {monster.Name} hits you for {result.MonsterDamageDealt}.");

        if (result.PlayerDefeated)
        {
            lines.Add(HandleDefeat());
            _currentMonster = null;
        }

        return string.Join('\n', lines);
    }

    private string AwardVictory(Monster monster)
    {
        var lines = new List<string> { $"You gain {monster.RewardExperiencePoints} XP and {monster.RewardGold} gold." };
        Player.AddGold(monster.RewardGold);

        var levelsGained = Player.AddExperience(monster.RewardExperiencePoints);
        foreach (var newLevel in levelsGained)
        {
            lines.Add($"You reached level {newLevel}! Max HP is now {Player.MaximumHitPoints}.");
        }

        var loot = _combat.RollLoot(monster);
        foreach (var item in loot)
        {
            Player.Inventory.AddItem(item);
            lines.Add($"You picked up: {item.Name}.");
        }

        return string.Join('\n', lines);
    }

    private string HandleDefeat()
    {
        Player.ReviveAfterDefeat(GameWorld.HomeLocationId);
        return $"You collapse and wake up at Home, poorer for it. Gold remaining: {Player.Gold}.";
    }

    private string Flee()
    {
        if (_currentMonster is not { IsAlive: true } monster)
        {
            return "There's nothing here to flee from.";
        }

        if (_combat.RollFleeSuccess())
        {
            _currentMonster = null;
            return "You break away and escape.";
        }

        var damage = _combat.MonsterFreeAttack(Player, monster);
        var lines = new List<string> { $"You fail to escape! The {monster.Name} hits you for {damage} on the way." };

        if (!Player.IsAlive)
        {
            lines.Add(HandleDefeat());
            _currentMonster = null;
        }

        return string.Join('\n', lines);
    }

    private string Inventory()
    {
        var lines = new List<string>
        {
            $"Gold: {Player.Gold}",
            $"Weapon: {(Player.EquippedWeapon?.Name ?? "(bare hands)")}",
            $"Armor: {(Player.EquippedArmor?.Name ?? "(none)")}",
        };

        if (Player.Inventory.Slots.Count == 0)
        {
            lines.Add("Bag is empty.");
        }
        else
        {
            lines.Add("Bag:");
            lines.AddRange(Player.Inventory.Slots.Select(slot => $"  {slot.Item.Name} x{slot.Quantity}"));
        }

        return string.Join('\n', lines);
    }

    private string Equip(string itemName)
    {
        var item = Player.Inventory.Find(itemName);
        return item switch
        {
            Weapon weapon => Try(() => Player.EquipWeapon(weapon), $"You equip the {weapon.Name}."),
            Armor armor => Try(() => Player.EquipArmor(armor), $"You equip the {armor.Name}."),
            null => $"You don't have a '{itemName}'.",
            _ => $"The {item.Name} can't be equipped.",
        };
    }

    private string Use(string itemName)
    {
        var item = Player.Inventory.Find(itemName);
        if (item is not Consumable consumable)
        {
            return item is null ? $"You don't have a '{itemName}'." : $"The {item.Name} can't be used that way.";
        }

        Player.Inventory.RemoveItem(consumable.Name);
        Player.Heal(consumable.HealAmount);
        return $"You use the {consumable.Name} and recover {consumable.HealAmount} HP. ({Player.CurrentHitPoints}/{Player.MaximumHitPoints})";
    }

    private string Status() =>
        $"{Player.Name} -- Level {Player.Level}, {Player.CurrentHitPoints}/{Player.MaximumHitPoints} HP, " +
        $"{Player.ExperiencePoints} XP, {Player.Gold} gold.";

    private string Quests()
    {
        if (Player.Quests.Count == 0) return "No quests accepted yet.";

        return string.Join('\n', Player.Quests.Select(pq =>
        {
            var status = pq.IsCompleted ? "complete" : "in progress";
            var objectives = string.Join(", ", pq.Quest.Objectives.Select(o => $"{o.ItemName} x{o.Quantity}"));
            return $"{pq.Quest.Name} ({status}): {objectives}";
        }));
    }

    private string AcceptQuest()
    {
        var quest = CurrentLocation.QuestOfferedHere;
        if (quest is null) return "No quest is offered here.";
        if (Player.FindQuest(quest.Id) is not null) return "You've already accepted this quest.";

        Player.Quests.Add(new PlayerQuest(quest));
        return $"Accepted: '{quest.Name}' -- {quest.Description}";
    }

    private string TurnInQuest()
    {
        var quest = CurrentLocation.QuestOfferedHere;
        if (quest is null) return "There's no quest to turn in here.";

        var playerQuest = Player.FindQuest(quest.Id);
        if (playerQuest is null) return "You haven't accepted this quest.";
        if (playerQuest.IsCompleted) return "You've already completed this quest.";
        if (!quest.IsSatisfiedBy(Player.Inventory)) return "You don't have everything this quest needs yet.";

        foreach (var objective in quest.Objectives.Where(o => o.ConsumedOnCompletion))
        {
            Player.Inventory.RemoveItem(objective.ItemName, objective.Quantity);
        }

        playerQuest.IsCompleted = true;
        Player.AddGold(quest.RewardGold);
        var lines = new List<string> { $"Quest complete: '{quest.Name}'! +{quest.RewardExperiencePoints} XP, +{quest.RewardGold} gold." };

        var levelsGained = Player.AddExperience(quest.RewardExperiencePoints);
        foreach (var newLevel in levelsGained)
        {
            lines.Add($"You reached level {newLevel}! Max HP is now {Player.MaximumHitPoints}.");
        }

        if (quest.RewardItem is { } rewardItem)
        {
            Player.Inventory.AddItem(rewardItem);
            lines.Add($"You receive: {rewardItem.Name}.");
        }

        return string.Join('\n', lines);
    }

    private string Buy(string itemName)
    {
        var item = CurrentLocation.VendorInventory.FirstOrDefault(i =>
            string.Equals(i.Name, itemName, StringComparison.OrdinalIgnoreCase));
        if (item is null) return "That's not for sale here.";

        if (!Player.SpendGold(item.Value))
        {
            return $"You can't afford the {item.Name} ({item.Value}g, you have {Player.Gold}g).";
        }

        Player.Inventory.AddItem(item);
        return $"Bought {item.Name} for {item.Value}g.";
    }

    private string Sell(string itemName)
    {
        var item = Player.Inventory.Find(itemName);
        if (item is null) return $"You don't have a '{itemName}'.";

        var sellPrice = item.Value / 2;
        Player.Inventory.RemoveItem(item.Name);
        Player.AddGold(sellPrice);
        return $"Sold {item.Name} for {sellPrice}g.";
    }

    private static string Try(Action action, string successMessage)
    {
        try
        {
            action();
            return successMessage;
        }
        catch (InvalidOperationException ex)
        {
            return ex.Message;
        }
    }

    private static string Help() =>
        "Commands: look, move <dir>|n/s/e/w, attack, flee, inventory, equip <item>, " +
        "use <item>, status, quests, accept, turnin, buy <item>, sell <item>, quit";
}
