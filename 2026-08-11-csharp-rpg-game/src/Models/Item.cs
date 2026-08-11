namespace RpgGame.Models;

public abstract class Item
{
    public string Name { get; }
    public string Description { get; }
    public int Value { get; }

    protected Item(string name, string description, int value)
    {
        Name = name;
        Description = description;
        Value = value;
    }
}

public sealed class Weapon : Item
{
    public int MinDamage { get; }
    public int MaxDamage { get; }

    public Weapon(string name, string description, int value, int minDamage, int maxDamage)
        : base(name, description, value)
    {
        if (minDamage > maxDamage)
        {
            throw new ArgumentException($"{name}: MinDamage ({minDamage}) cannot exceed MaxDamage ({maxDamage})");
        }

        MinDamage = minDamage;
        MaxDamage = maxDamage;
    }
}

public sealed class Armor : Item
{
    public int DefenseBonus { get; }

    public Armor(string name, string description, int value, int defenseBonus)
        : base(name, description, value)
    {
        DefenseBonus = defenseBonus;
    }
}

public sealed class Consumable : Item
{
    public int HealAmount { get; }

    public Consumable(string name, string description, int value, int healAmount)
        : base(name, description, value)
    {
        HealAmount = healAmount;
    }
}

// Items that exist only to be handed in for a quest (e.g. "5 rat tails").
// They have no combat or consumption effect of their own.
public sealed class QuestObjectiveItem : Item
{
    public QuestObjectiveItem(string name, string description, int value)
        : base(name, description, value)
    {
    }
}
