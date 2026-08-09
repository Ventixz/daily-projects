import java.util.Objects;

public class TestHelper {
    static int passed = 0;
    static int failed = 0;

    public static void assertEquals(Object expected, Object actual, String message) {
        if (Objects.equals(expected, actual)) {
            passed++;
        } else {
            failed++;
            System.out.println("FAIL: " + message + "\n  expected: " + expected + "\n  actual:   " + actual);
        }
    }

    public static void assertTrue(boolean condition, String message) {
        if (condition) {
            passed++;
        } else {
            failed++;
            System.out.println("FAIL: " + message);
        }
    }

    public static void assertNull(Object value, String message) {
        assertTrue(value == null, message + " (was " + value + ")");
    }
}
