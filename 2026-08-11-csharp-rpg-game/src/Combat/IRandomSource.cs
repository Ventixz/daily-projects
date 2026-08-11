namespace RpgGame.Combat;

// Combat and loot rolls go through this instead of System.Random directly
// so tests can hand in a fixed sequence and assert an exact outcome instead
// of a probabilistic range.
public interface IRandomSource
{
    // Both bounds inclusive.
    int Next(int minInclusive, int maxInclusive);
}

public sealed class SystemRandomSource : IRandomSource
{
    private readonly Random _random = new();

    public int Next(int minInclusive, int maxInclusive) => _random.Next(minInclusive, maxInclusive + 1);
}
