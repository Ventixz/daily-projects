namespace RpgGame.Tests;

public static class TestRunner
{
    public static int Main()
    {
        InventoryTests.Run();
        PlayerTests.Run();
        CombatEngineTests.Run();
        QuestTests.Run();
        WorldTests.Run();
        GameFlowTests.Run();

        Console.WriteLine();
        Console.WriteLine($"{TestHelper.Passed} passed, {TestHelper.Failed} failed");
        return TestHelper.Failed > 0 ? 1 : 0;
    }
}
