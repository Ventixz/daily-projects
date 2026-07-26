import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.util.List;

/** Runs a snippet of Lox source through a fresh Scanner/Parser/Interpreter and captures whatever it printed. */
class TestHelper {
    static String run(String source) {
        PrintStream originalOut = System.out;
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        System.setOut(new PrintStream(captured));

        try {
            Lox.hadError = false;
            Lox.hadRuntimeError = false;

            Scanner scanner = new Scanner(source);
            List<Token> tokens = scanner.scanTokens();
            Parser parser = new Parser(tokens);
            List<Stmt> statements = parser.parse();

            new Interpreter().interpret(statements);
        } finally {
            System.setOut(originalOut);
        }

        return captured.toString().trim();
    }

    static List<Token> scan(String source) {
        return new Scanner(source).scanTokens();
    }
}
