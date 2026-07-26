class InterpreterTests {
    static void run() {
        testArithmeticPrecedence();
        testStringConcatenation();
        testMixedPlusStringifiesTheOtherOperand();
        testComparisonAndEquality();
        testVariablesAndAssignment();
        testBlockScopingShadowsOuterVariable();
        testAssignmentTargetsEnclosingScope();
        testIfElseBranching();
        testWhileLoop();
        testForLoopDesugaring();
        testLogicalOperatorsShortCircuitAndReturnOperand();
        testFunctionCallAndReturn();
        testRecursiveFunction();
        testClosureCapturesEnclosingVariable();
        testEachClosureInstanceGetsItsOwnState();
        testDivisionByZeroIsARuntimeError();
        testUndefinedVariableIsARuntimeError();
        testTypeErrorOnBadOperands();
    }

    private static void testArithmeticPrecedence() {
        String out = TestHelper.run("print 2 + 3 * 4;");
        TestRunner.assertEquals("14", out, "interp_arithmetic_precedence");
    }

    private static void testStringConcatenation() {
        String out = TestHelper.run("print \"foo\" + \"bar\";");
        TestRunner.assertEquals("foobar", out, "interp_string_concatenation");
    }

    private static void testMixedPlusStringifiesTheOtherOperand() {
        String out = TestHelper.run("print \"count: \" + 3;");
        TestRunner.assertEquals("count: 3", out, "interp_plus_number_onto_string");
    }

    private static void testComparisonAndEquality() {
        String out = TestHelper.run("print 1 < 2; print 2 <= 2; print 3 == 3; print 3 != 3;");
        TestRunner.assertEquals("true\ntrue\ntrue\nfalse", out, "interp_comparison_and_equality");
    }

    private static void testVariablesAndAssignment() {
        String out = TestHelper.run("var a = 1; a = a + 1; print a;");
        TestRunner.assertEquals("2", out, "interp_variable_reassignment");
    }

    private static void testBlockScopingShadowsOuterVariable() {
        String out = TestHelper.run(
                "var a = \"outer\"; { var a = \"inner\"; print a; } print a;");
        TestRunner.assertEquals("inner\nouter", out, "interp_block_scope_shadowing");
    }

    private static void testAssignmentTargetsEnclosingScope() {
        String out = TestHelper.run(
                "var a = 1; { a = 2; } print a;");
        TestRunner.assertEquals("2", out, "interp_assignment_reaches_enclosing_scope");
    }

    private static void testIfElseBranching() {
        String out = TestHelper.run(
                "if (1 < 2) { print \"yes\"; } else { print \"no\"; }");
        TestRunner.assertEquals("yes", out, "interp_if_true_branch");

        out = TestHelper.run(
                "if (1 > 2) { print \"yes\"; } else { print \"no\"; }");
        TestRunner.assertEquals("no", out, "interp_if_false_branch");
    }

    private static void testWhileLoop() {
        String out = TestHelper.run(
                "var i = 0; while (i < 3) { print i; i = i + 1; }");
        TestRunner.assertEquals("0\n1\n2", out, "interp_while_loop");
    }

    private static void testForLoopDesugaring() {
        String out = TestHelper.run(
                "for (var i = 0; i < 3; i = i + 1) { print i; }");
        TestRunner.assertEquals("0\n1\n2", out, "interp_for_loop");
    }

    private static void testLogicalOperatorsShortCircuitAndReturnOperand() {
        // `or` returns the first truthy operand without evaluating the right side.
        String out = TestHelper.run("print \"left\" or (1/0);");
        TestRunner.assertEquals("left", out, "interp_or_short_circuits");

        out = TestHelper.run("print false and (1/0);");
        TestRunner.assertEquals("false", out, "interp_and_short_circuits");
    }

    private static void testFunctionCallAndReturn() {
        String out = TestHelper.run(
                "fun add(a, b) { return a + b; } print add(2, 3);");
        TestRunner.assertEquals("5", out, "interp_function_call_and_return");
    }

    private static void testRecursiveFunction() {
        String out = TestHelper.run(
                "fun fact(n) { if (n <= 1) return 1; return n * fact(n - 1); } print fact(5);");
        TestRunner.assertEquals("120", out, "interp_recursive_function");
    }

    private static void testClosureCapturesEnclosingVariable() {
        String out = TestHelper.run(
                "fun makeAdder(x) { fun adder(y) { return x + y; } return adder; } " +
                "var addFive = makeAdder(5); print addFive(2);");
        TestRunner.assertEquals("7", out, "interp_closure_captures_variable");
    }

    private static void testEachClosureInstanceGetsItsOwnState() {
        String out = TestHelper.run(
                "fun makeCounter() { var count = 0; fun inc() { count = count + 1; return count; } return inc; } " +
                "var a = makeCounter(); var b = makeCounter(); print a(); print a(); print b();");
        TestRunner.assertEquals("1\n2\n1", out, "interp_closures_have_independent_state");
    }

    private static void testDivisionByZeroIsARuntimeError() {
        Lox.hadRuntimeError = false;
        TestHelper.run("print 1 / 0;");
        TestRunner.assertTrue(Lox.hadRuntimeError, "interp_division_by_zero_is_runtime_error");
    }

    private static void testUndefinedVariableIsARuntimeError() {
        Lox.hadRuntimeError = false;
        TestHelper.run("print doesNotExist;");
        TestRunner.assertTrue(Lox.hadRuntimeError, "interp_undefined_variable_is_runtime_error");
    }

    private static void testTypeErrorOnBadOperands() {
        Lox.hadRuntimeError = false;
        TestHelper.run("print 1 - \"nope\";");
        TestRunner.assertTrue(Lox.hadRuntimeError, "interp_bad_operand_type_is_runtime_error");
    }
}
