public class TestRunner {
    public static void main(String[] args) throws Exception {
        HttpRequestParserTests.run();
        RouterTests.run();
        StaticFileHandlerTests.run();
        EndToEndTests.run();

        System.out.println();
        System.out.println(TestHelper.passed + " passed, " + TestHelper.failed + " failed");
        if (TestHelper.failed > 0) System.exit(1);
    }
}
