/** Minimal hand-rolled test harness: no JUnit, just assert-and-print, same spirit as this repo's other projects. */
class TestRunner {
    private static int passed = 0;
    private static int failed = 0;

    static void assertEquals(Object expected, Object actual, String testName) {
        boolean equal = (expected == null) ? (actual == null) : expected.equals(actual);
        if (equal) {
            passed++;
            System.out.println("PASS " + testName);
        } else {
            failed++;
            System.out.println("FAIL " + testName + " (expected <" + expected + "> but was <" + actual + ">)");
        }
    }

    static void assertTrue(boolean condition, String testName) {
        assertEquals(true, condition, testName);
    }

    public static void main(String[] args) {
        ScannerTests.run();
        InterpreterTests.run();

        System.out.println();
        System.out.println(passed + " passed, " + failed + " failed");
        if (failed > 0) System.exit(1);
    }
}
