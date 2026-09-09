import random
import unittest

from src.parser import parse
from src.interpreter import evaluate
from src.jit import compile_expr, JitFunction
from src.codegen import compile_to_machine_code


def jit_eval(text, variables=None):
    return compile_expr(text).call(variables or {})


class TestJit(unittest.TestCase):
    def test_literal(self):
        self.assertEqual(jit_eval("42"), 42)

    def test_negative_literal_round_trips(self):
        # mov rax, imm64 stores the value two's-complement-wrapped into 8
        # bytes; this checks it comes back out as the original negative
        # int, not as the huge unsigned value the encoder actually wrote.
        self.assertEqual(jit_eval("-123456789"), -123456789)

    def test_addition_subtraction_multiplication(self):
        self.assertEqual(jit_eval("3 + 4 * (2 - 5)"), -9)

    def test_truncating_division_matches_interpreter(self):
        self.assertEqual(jit_eval("-7 / 2"), -3)
        self.assertEqual(jit_eval("7 / -2"), -3)

    def test_variables_map_to_correct_slots(self):
        # a and z are the first and last of 26 slots -- if slot indexing or
        # the disp32 encoding were off by a register-sized stride, this is
        # the pair most likely to catch it.
        self.assertEqual(jit_eval("a - z", {"a": 100, "z": 1}), 99)

    def test_missing_variable_defaults_to_zero(self):
        self.assertEqual(jit_eval("a + 1", {}), 1)

    def test_unary_minus(self):
        self.assertEqual(jit_eval("-(3 + 4)"), -7)

    def test_reused_jit_function_is_pure(self):
        # the same compiled buffer, called twice with different bindings,
        # must not carry state between calls (e.g. through an un-zeroed slot).
        fn = compile_expr("a * 2")
        self.assertEqual(fn.call({"a": 3}), 6)
        self.assertEqual(fn.call({"a": 10}), 20)

    def test_non_lowercase_variable_name_rejected(self):
        fn = compile_expr("a")
        with self.assertRaises(ValueError):
            fn.call({"A": 1})


def _random_expr(rng, depth):
    if depth <= 0 or rng.random() < 0.4:
        if rng.random() < 0.5:
            return str(rng.randint(-50, 50))
        return rng.choice("abc")
    op = rng.choice("+-*/")
    left = _random_expr(rng, depth - 1)
    right = _random_expr(rng, depth - 1)
    if op == "/":
        # dodge the one case this JIT can't survive: IDIV by zero raises
        # SIGFPE in hardware, which Python has no way to catch -- it would
        # kill the test process instead of failing one test. Documented as
        # a deliberate scope cut in LEARNING.md.
        right = f"({right} + 51)"  # shifts any value in [-50, 50] to [1, 101]
    return f"({left} {op} {right})"


class TestJitFuzzAgainstInterpreter(unittest.TestCase):
    def test_fuzz_jit_matches_interpreter(self):
        for seed in range(50):
            rng = random.Random(seed)
            expr_text = _random_expr(rng, depth=4)
            variables = {name: rng.randint(-20, 20) for name in "abc"}
            ast = parse(expr_text)
            expected = evaluate(ast, variables)
            actual = JitFunction(compile_to_machine_code(ast)).call(variables)
            with self.subTest(expr=expr_text, variables=variables):
                self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
