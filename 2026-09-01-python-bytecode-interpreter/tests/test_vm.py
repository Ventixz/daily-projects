import io
import contextlib
import unittest

from src.vm import VirtualMachine, VMError, VMRuntimeError


def run(source, global_ns=None):
    """Compile `source` with the real `compile()` and execute the
    resulting code object on our VM, returning the resulting globals."""
    code = compile(source, "<test>", "exec")
    return VirtualMachine().run(code, global_ns)


def run_capturing_stdout(source):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ns = run(source)
    return ns, buf.getvalue()


class TestArithmeticAndComparisons(unittest.TestCase):
    def test_arithmetic_precedence_and_binary_op_dispatch(self):
        ns = run("x = 1 + 2 * 3 - 4 / 2\n")
        self.assertEqual(ns["x"], 1 + 2 * 3 - 4 / 2)

    def test_floor_div_mod_pow(self):
        ns = run("a = 7 // 2\nb = 7 % 2\nc = 2 ** 10\n")
        self.assertEqual((ns["a"], ns["b"], ns["c"]), (3, 1, 1024))

    def test_augmented_assignment_uses_inplace_binary_op(self):
        ns = run("x = 10\nx += 5\nx -= 2\nx *= 3\n")
        self.assertEqual(ns["x"], 39)

    def test_unary_operators(self):
        ns = run("a = -5\nb = +5\nc = not False\nd = ~0\n")
        self.assertEqual((ns["a"], ns["b"], ns["c"], ns["d"]), (-5, 5, True, -1))

    def test_comparisons(self):
        ns = run(
            "a = 1 < 2\n"
            "b = 2 <= 2\n"
            "c = 3 == 4\n"
            "d = 3 != 4\n"
            "e = 5 > 4\n"
            "f = 5 >= 6\n"
        )
        self.assertEqual(
            [ns[k] for k in "abcdef"], [True, True, False, True, True, False]
        )

    def test_is_and_is_not(self):
        ns = run("x = None\na = x is None\nb = x is not None\n")
        self.assertEqual((ns["a"], ns["b"]), (True, False))

    def test_in_and_not_in(self):
        ns = run("a = 2 in [1, 2, 3]\nb = 5 not in [1, 2, 3]\n")
        self.assertEqual((ns["a"], ns["b"]), (True, True))

    def test_short_circuit_and_or_return_operand_not_bool(self):
        ns = run("a = 0 and 99\nb = 3 and 99\nc = 0 or 42\nd = 5 or 42\n")
        self.assertEqual((ns["a"], ns["b"], ns["c"], ns["d"]), (0, 99, 42, 5))


class TestControlFlow(unittest.TestCase):
    def test_if_elif_else(self):
        source = (
            "def classify(n):\n"
            "    if n < 0:\n"
            "        return 'negative'\n"
            "    elif n == 0:\n"
            "        return 'zero'\n"
            "    else:\n"
            "        return 'positive'\n"
            "a = classify(-1)\n"
            "b = classify(0)\n"
            "c = classify(1)\n"
        )
        ns = run(source)
        self.assertEqual((ns["a"], ns["b"], ns["c"]), ("negative", "zero", "positive"))

    def test_while_loop(self):
        ns = run("total = 0\nn = 5\nwhile n > 0:\n    total += n\n    n -= 1\n")
        self.assertEqual(ns["total"], 15)

    def test_for_loop_over_range(self):
        ns = run("total = 0\nfor i in range(10):\n    total += i\n")
        self.assertEqual(ns["total"], sum(range(10)))

    def test_for_loop_over_list_and_string(self):
        ns = run(
            "letters = []\n"
            "for ch in 'abc':\n"
            "    letters = letters + [ch]\n"
            "nums = [1, 2, 3]\n"
            "total = 0\n"
            "for n in nums:\n"
            "    total += n\n"
        )
        self.assertEqual(ns["letters"], ["a", "b", "c"])
        self.assertEqual(ns["total"], 6)

    def test_nested_loops_and_break_free_early_exit_via_flag(self):
        # No BREAK/CONTINUE support (see LEARNING.md) -- early exit is
        # expressed with an ordinary flag variable instead.
        source = (
            "found = None\n"
            "i = 0\n"
            "while found is None and i < 5:\n"
            "    j = 0\n"
            "    while found is None and j < 5:\n"
            "        if i * j == 6:\n"
            "            found = (i, j)\n"
            "        j += 1\n"
            "    i += 1\n"
        )
        ns = run(source)
        self.assertEqual(ns["found"], (2, 3))


class TestFunctions(unittest.TestCase):
    def test_simple_function_call(self):
        ns = run("def add(a, b):\n    return a + b\nresult = add(2, 3)\n")
        self.assertEqual(ns["result"], 5)

    def test_default_arguments(self):
        ns = run(
            "def greet(name, greeting='Hello'):\n"
            "    return greeting + ', ' + name\n"
            "a = greet('World')\n"
            "b = greet('World', 'Hi')\n"
        )
        self.assertEqual(ns["a"], "Hello, World")
        self.assertEqual(ns["b"], "Hi, World")

    def test_keyword_arguments(self):
        ns = run(
            "def power(base, exponent=2):\n"
            "    return base ** exponent\n"
            "a = power(3)\n"
            "b = power(base=2, exponent=5)\n"
            "c = power(2, exponent=10)\n"
        )
        self.assertEqual((ns["a"], ns["b"], ns["c"]), (9, 32, 1024))

    def test_recursion_factorial(self):
        ns = run(
            "def factorial(n):\n"
            "    if n <= 1:\n"
            "        return 1\n"
            "    return n * factorial(n - 1)\n"
            "result = factorial(10)\n"
        )
        import math
        self.assertEqual(ns["result"], math.factorial(10))

    def test_recursion_fibonacci(self):
        ns = run(
            "def fib(n):\n"
            "    if n < 2:\n"
            "        return n\n"
            "    return fib(n - 1) + fib(n - 2)\n"
            "result = fib(15)\n"
        )
        self.assertEqual(ns["result"], 610)

    def test_mutual_helper_functions(self):
        ns = run(
            "def double(x):\n"
            "    return x * 2\n"
            "def quadruple(x):\n"
            "    return double(double(x))\n"
            "result = quadruple(5)\n"
        )
        self.assertEqual(ns["result"], 20)

    def test_missing_required_argument_raises(self):
        with self.assertRaises(VMRuntimeError):
            run("def f(a, b):\n    return a + b\nf(1)\n")

    def test_too_many_positional_arguments_raises(self):
        with self.assertRaises(VMRuntimeError):
            run("def f(a):\n    return a\nf(1, 2)\n")

    def test_unexpected_keyword_argument_raises(self):
        with self.assertRaises(VMRuntimeError):
            run("def f(a):\n    return a\nf(a=1, b=2)\n")


class TestContainers(unittest.TestCase):
    def test_list_tuple_set_dict_literals(self):
        ns = run(
            "lst = [1, 2, 3]\n"
            "tup = (1, 2, 3)\n"
            "st = {1, 2, 2, 3}\n"
            "d = {'a': 1, 'b': 2}\n"
        )
        self.assertEqual(ns["lst"], [1, 2, 3])
        self.assertEqual(ns["tup"], (1, 2, 3))
        self.assertEqual(ns["st"], {1, 2, 3})
        self.assertEqual(ns["d"], {"a": 1, "b": 2})

    def test_subscript_get_and_set(self):
        ns = run(
            "lst = [10, 20, 30]\n"
            "x = lst[1]\n"
            "d = {}\n"
            "d['key'] = x\n"
            "y = d['key']\n"
        )
        self.assertEqual(ns["x"], 20)
        self.assertEqual(ns["d"], {"key": 20})
        self.assertEqual(ns["y"], 20)

    def test_tuple_unpacking_and_swap(self):
        ns = run("a, b = 1, 2\na, b = b, a\nx, y, z = (10, 20, 30)\n")
        self.assertEqual((ns["a"], ns["b"]), (2, 1))
        self.assertEqual((ns["x"], ns["y"], ns["z"]), (10, 20, 30))

    def test_unpacking_wrong_length_raises(self):
        with self.assertRaises(VMRuntimeError):
            run("a, b = (1, 2, 3)\n")


class TestBuiltinsAndGlobals(unittest.TestCase):
    def test_calling_real_builtins(self):
        ns = run(
            "a = len([1, 2, 3])\n"
            "b = sum([1, 2, 3])\n"
            "c = max(1, 5, 3)\n"
            "d = sorted([3, 1, 2])\n"
            "e = list(range(3))\n"
        )
        self.assertEqual(ns["a"], 3)
        self.assertEqual(ns["b"], 6)
        self.assertEqual(ns["c"], 5)
        self.assertEqual(ns["d"], [1, 2, 3])
        self.assertEqual(ns["e"], [0, 1, 2])

    def test_print_reaches_real_stdout(self):
        _, out = run_capturing_stdout("print('hello', 1, [1, 2])\n")
        self.assertEqual(out, "hello 1 [1, 2]\n")

    def test_global_statement_mutates_module_global(self):
        ns = run(
            "counter = 0\n"
            "def increment():\n"
            "    global counter\n"
            "    counter += 1\n"
            "increment()\n"
            "increment()\n"
            "increment()\n"
        )
        self.assertEqual(ns["counter"], 3)

    def test_string_formatting_with_percent_operator(self):
        ns = run("x = 42\ns = 'value: %d' % x\n")
        self.assertEqual(ns["s"], "value: 42")

    def test_undefined_name_raises(self):
        with self.assertRaises(VMRuntimeError):
            run("print(does_not_exist)\n")


class TestScopeCuts(unittest.TestCase):
    """Confirm that out-of-scope features fail loudly (VMError) rather
    than silently doing the wrong thing."""

    def test_classes_are_unsupported(self):
        with self.assertRaises(VMError):
            run("class Foo:\n    pass\n")

    def test_try_except_does_not_actually_catch(self):
        # CPython 3.11 tracks try/except regions in a side table
        # (`co_exceptiontable`), not in the bytecode stream itself, so
        # a try body that runs clean is bytecode-indistinguishable from
        # one with no try at all -- this VM only breaks once something
        # actually raises, and then the real Python exception just
        # propagates straight out, uncaught, since nothing here ever
        # consults that side table.
        with self.assertRaises(ZeroDivisionError):
            run("try:\n    x = 1 / 0\nexcept ZeroDivisionError:\n    x = 2\n")

    def test_attribute_access_is_unsupported(self):
        with self.assertRaises(VMError):
            run("x = [1, 2]\nx.append(3)\n")

    def test_list_comprehension_is_unsupported(self):
        with self.assertRaises(VMError):
            run("x = [i * 2 for i in range(3)]\n")

    def test_fstring_is_unsupported(self):
        with self.assertRaises(VMError):
            run("x = 1\ny = f'{x}'\n")

    def test_closures_are_unsupported(self):
        with self.assertRaises(VMError):
            run(
                "def outer():\n"
                "    x = 1\n"
                "    def inner():\n"
                "        return x\n"
                "    return inner\n"
                "outer()()\n"
            )


class TestAgainstRealCPython(unittest.TestCase):
    """For every program in this list, running it on our VM must agree
    with just handing it to real Python's own `exec`."""

    PROGRAMS = [
        "x = (1 + 2) * 3 - 4 // 2\n",
        "total = 0\nfor i in range(20):\n    if i % 2 == 0:\n        total += i\n    else:\n        total -= 1\n",
        "def fib(n):\n    a, b = 0, 1\n    i = 0\n    while i < n:\n        a, b = b, a + b\n        i += 1\n    return a\nresult = fib(20)\n",
        "words = ['pear', 'kiwi', 'fig', 'date']\ncounts = {}\nfor w in words:\n    length = len(w)\n    if length in counts:\n        counts[length] = counts[length] + 1\n    else:\n        counts[length] = 1\n",
        "def is_prime(n):\n    if n < 2:\n        return False\n    i = 2\n    is_p = True\n    while i * i <= n:\n        if n % i == 0:\n            is_p = False\n        i += 1\n    return is_p\nprimes = []\nfor n in range(30):\n    if is_prime(n):\n        primes = primes + [n]\n",
    ]

    def test_matches_real_exec(self):
        for source in self.PROGRAMS:
            with self.subTest(source=source):
                vm_ns = run(source)
                real_ns: dict = {}
                exec(source, real_ns)
                for name, value in real_ns.items():
                    if name == "__builtins__" or callable(value):
                        continue  # our Function stand-ins don't compare equal to real ones
                    self.assertEqual(vm_ns.get(name), value, msg=f"mismatch on {name!r}")


if __name__ == "__main__":
    unittest.main()
