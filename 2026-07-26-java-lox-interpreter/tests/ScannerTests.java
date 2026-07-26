import java.util.List;

class ScannerTests {
    static void run() {
        testSingleCharacterTokens();
        testTwoCharacterOperators();
        testKeywordsVsIdentifiers();
        testStringLiteral();
        testUnterminatedStringLiteral();
        testNumberLiteral();
        testLineCommentsAreIgnored();
        testLineNumbersAdvanceAcrossNewlines();
    }

    private static void testSingleCharacterTokens() {
        List<Token> tokens = TestHelper.scan("(){},.-+;*");
        // 10 punctuation tokens + EOF
        TestRunner.assertEquals(11, tokens.size(), "scanner_single_character_token_count");
        TestRunner.assertEquals(TokenType.LEFT_PAREN, tokens.get(0).type, "scanner_left_paren");
        TestRunner.assertEquals(TokenType.RIGHT_BRACE, tokens.get(3).type, "scanner_right_brace");
        TestRunner.assertEquals(TokenType.EOF, tokens.get(tokens.size() - 1).type, "scanner_ends_with_eof");
    }

    private static void testTwoCharacterOperators() {
        List<Token> tokens = TestHelper.scan("!= == <= >= < > = !");
        TestRunner.assertEquals(TokenType.BANG_EQUAL, tokens.get(0).type, "scanner_bang_equal");
        TestRunner.assertEquals(TokenType.EQUAL_EQUAL, tokens.get(1).type, "scanner_equal_equal");
        TestRunner.assertEquals(TokenType.LESS_EQUAL, tokens.get(2).type, "scanner_less_equal");
        TestRunner.assertEquals(TokenType.GREATER_EQUAL, tokens.get(3).type, "scanner_greater_equal");
        TestRunner.assertEquals(TokenType.LESS, tokens.get(4).type, "scanner_less_alone");
        TestRunner.assertEquals(TokenType.GREATER, tokens.get(5).type, "scanner_greater_alone");
        TestRunner.assertEquals(TokenType.EQUAL, tokens.get(6).type, "scanner_equal_alone");
        TestRunner.assertEquals(TokenType.BANG, tokens.get(7).type, "scanner_bang_alone");
    }

    private static void testKeywordsVsIdentifiers() {
        List<Token> tokens = TestHelper.scan("var x = while");
        TestRunner.assertEquals(TokenType.VAR, tokens.get(0).type, "scanner_var_is_keyword");
        TestRunner.assertEquals(TokenType.IDENTIFIER, tokens.get(1).type, "scanner_x_is_identifier");
        TestRunner.assertEquals(TokenType.WHILE, tokens.get(3).type, "scanner_while_is_keyword");
    }

    private static void testStringLiteral() {
        List<Token> tokens = TestHelper.scan("\"hello world\"");
        TestRunner.assertEquals(TokenType.STRING, tokens.get(0).type, "scanner_string_token_type");
        TestRunner.assertEquals("hello world", tokens.get(0).literal, "scanner_string_literal_value");
    }

    private static void testUnterminatedStringLiteral() {
        Lox.hadError = false;
        TestHelper.scan("\"never closed");
        TestRunner.assertTrue(Lox.hadError, "scanner_unterminated_string_reports_error");
    }

    private static void testNumberLiteral() {
        List<Token> tokens = TestHelper.scan("42 3.14");
        TestRunner.assertEquals(42.0, tokens.get(0).literal, "scanner_integer_literal_value");
        TestRunner.assertEquals(3.14, tokens.get(1).literal, "scanner_decimal_literal_value");
    }

    private static void testLineCommentsAreIgnored() {
        List<Token> tokens = TestHelper.scan("var x = 1; // this is a comment\nvar y = 2;");
        long identifierCount = tokens.stream().filter(t -> t.type == TokenType.IDENTIFIER).count();
        TestRunner.assertEquals(2L, identifierCount, "scanner_comment_produces_no_tokens");
    }

    private static void testLineNumbersAdvanceAcrossNewlines() {
        List<Token> tokens = TestHelper.scan("var x = 1;\nvar y = 2;");
        // tokens: var x = 1 ; var y = 2 ; EOF -> "y" is index 6
        Token y = tokens.get(6);
        TestRunner.assertEquals("y", y.lexeme, "scanner_finds_y_token");
        TestRunner.assertEquals(2, y.line, "scanner_tracks_line_number_after_newline");
    }
}
