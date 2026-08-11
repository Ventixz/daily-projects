using RpgGame.Combat;

namespace RpgGame.Tests;

// Hands back a fixed, ordered sequence of values regardless of the
// requested range -- tests choose values that are valid for whatever call
// they're standing in for (e.g. a weapon's actual min/max) so the fake
// doesn't need to know which call site it's serving.
public sealed class FakeRandomSource : IRandomSource
{
    private readonly Queue<int> _values;

    public FakeRandomSource(params int[] values)
    {
        _values = new Queue<int>(values);
    }

    public int Next(int minInclusive, int maxInclusive)
    {
        if (_values.Count == 0)
        {
            throw new InvalidOperationException("FakeRandomSource ran out of queued values.");
        }

        return _values.Dequeue();
    }
}
