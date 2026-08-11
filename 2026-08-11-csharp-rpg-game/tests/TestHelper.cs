namespace RpgGame.Tests;

public static class TestHelper
{
    public static int Passed;
    public static int Failed;

    public static void AssertEquals<T>(T expected, T actual, string message)
    {
        if (Equals(expected, actual))
        {
            Passed++;
        }
        else
        {
            Failed++;
            Console.WriteLine($"FAIL: {message}\n  expected: {expected}\n  actual:   {actual}");
        }
    }

    public static void AssertTrue(bool condition, string message)
    {
        if (condition)
        {
            Passed++;
        }
        else
        {
            Failed++;
            Console.WriteLine($"FAIL: {message}");
        }
    }

    public static void AssertFalse(bool condition, string message) => AssertTrue(!condition, message);
}
