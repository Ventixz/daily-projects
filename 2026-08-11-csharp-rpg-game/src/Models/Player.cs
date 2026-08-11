namespace RpgGame.Models;

public sealed class Player
{
    private static readonly int[] ExperienceForNextLevel = { 100, 250, 500, 1000, 2000 };

    public string Name { get; }
    public int Level { get; private set; } = 1;
    public int ExperiencePoints { get; private set; }
    public int Gold { get; private set; }

    public int MaximumHitPoints { get; private set; } = 20;
    public int CurrentHitPoints { get; private set; } = 20;

    public Inventory Inventory { get; } = new();
    public Weapon? EquippedWeapon { get; private set; }
    public Armor? EquippedArmor { get; private set; }

    public string CurrentLocationId { get; set; }
    public HashSet<string> VisitedLocationIds { get; } = new();
    public List<PlayerQuest> Quests { get; } = new();

    public Player(string name, string startingLocationId, int startingGold = 10)
    {
        Name = name;
        CurrentLocationId = startingLocationId;
        VisitedLocationIds.Add(startingLocationId);
        Gold = startingGold;
    }

    public bool IsAlive => CurrentHitPoints > 0;

    public int AttackMinDamage => EquippedWeapon?.MinDamage ?? 1;
    public int AttackMaxDamage => EquippedWeapon?.MaxDamage ?? 2;
    public int DefenseBonus => EquippedArmor?.DefenseBonus ?? 0;

    // Equipping doesn't discard whatever was worn before -- it goes back
    // into the pack. Losing an item because you tried on a new one would
    // make "equip" a one-way, exploratory-unfriendly action.
    public void EquipWeapon(Weapon weapon)
    {
        if (!Inventory.HasItem(weapon.Name))
        {
            throw new InvalidOperationException($"{Name} doesn't have a {weapon.Name} to equip.");
        }

        Inventory.RemoveItem(weapon.Name);
        if (EquippedWeapon is not null)
        {
            Inventory.AddItem(EquippedWeapon);
        }

        EquippedWeapon = weapon;
    }

    public void EquipArmor(Armor armor)
    {
        if (!Inventory.HasItem(armor.Name))
        {
            throw new InvalidOperationException($"{Name} doesn't have a {armor.Name} to equip.");
        }

        Inventory.RemoveItem(armor.Name);
        if (EquippedArmor is not null)
        {
            Inventory.AddItem(EquippedArmor);
        }

        EquippedArmor = armor;
    }

    // Damage is never allowed to go below 1. A defense bonus that fully
    // cancels incoming damage would make some armor/monster pairings
    // unwinnable-but-unlosable stalemates instead of just "easier fights".
    public void TakeDamage(int rawDamage)
    {
        var mitigated = Math.Max(1, rawDamage - DefenseBonus);
        CurrentHitPoints = Math.Max(0, CurrentHitPoints - mitigated);
    }

    public void Heal(int amount)
    {
        CurrentHitPoints = Math.Min(MaximumHitPoints, CurrentHitPoints + amount);
    }

    public void AddGold(int amount) => Gold += amount;

    public bool SpendGold(int amount)
    {
        if (amount > Gold) return false;
        Gold -= amount;
        return true;
    }

    // Losing to a monster doesn't end the game -- it costs half your gold
    // (rounded down) and sends you back to full health at your last-visited
    // town, same as running from a fight gone wrong in most of these games.
    public void ReviveAfterDefeat(string reviveLocationId)
    {
        Gold -= Gold / 2;
        CurrentHitPoints = MaximumHitPoints;
        CurrentLocationId = reviveLocationId;
    }

    // Leveling can span more than one threshold in a single award (a big
    // quest reward landing after a fight already close to the next level),
    // so this loops rather than checking the boundary once.
    public List<int> AddExperience(int amount)
    {
        ExperiencePoints += amount;
        var levelsGained = new List<int>();

        while (Level <= ExperienceForNextLevel.Length &&
               ExperiencePoints >= ExperienceForNextLevel[Level - 1])
        {
            Level++;
            MaximumHitPoints += 10;
            CurrentHitPoints = MaximumHitPoints;
            levelsGained.Add(Level);
        }

        return levelsGained;
    }

    public PlayerQuest? FindQuest(string questId) =>
        Quests.FirstOrDefault(q => q.Quest.Id == questId);
}
