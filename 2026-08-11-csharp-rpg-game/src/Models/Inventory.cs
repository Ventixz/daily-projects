namespace RpgGame.Models;

public sealed class InventorySlot
{
    public Item Item { get; }
    public int Quantity { get; internal set; }

    public InventorySlot(Item item, int quantity)
    {
        Item = item;
        Quantity = quantity;
    }
}

public sealed class Inventory
{
    private readonly List<InventorySlot> _slots = new();

    public IReadOnlyList<InventorySlot> Slots => _slots;

    public void AddItem(Item item, int quantity = 1)
    {
        if (quantity <= 0) throw new ArgumentOutOfRangeException(nameof(quantity));

        var existing = _slots.FirstOrDefault(s => s.Item.Name == item.Name);
        if (existing is not null)
        {
            existing.Quantity += quantity;
        }
        else
        {
            _slots.Add(new InventorySlot(item, quantity));
        }
    }

    public bool HasItem(string itemName, int quantity = 1)
    {
        var slot = _slots.FirstOrDefault(s => s.Item.Name == itemName);
        return slot is not null && slot.Quantity >= quantity;
    }

    // Returns false (and removes nothing) if the inventory doesn't hold enough
    // of the item -- callers should check HasItem first if they need to
    // distinguish "nothing happened" from "partially happened".
    public bool RemoveItem(string itemName, int quantity = 1)
    {
        if (quantity <= 0) throw new ArgumentOutOfRangeException(nameof(quantity));

        var slot = _slots.FirstOrDefault(s => s.Item.Name == itemName);
        if (slot is null || slot.Quantity < quantity) return false;

        slot.Quantity -= quantity;
        if (slot.Quantity == 0)
        {
            _slots.Remove(slot);
        }

        return true;
    }

    public Item? Find(string itemName) => _slots.FirstOrDefault(s => s.Item.Name == itemName)?.Item;
}
