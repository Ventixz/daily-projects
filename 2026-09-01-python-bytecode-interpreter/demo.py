"""Runs a handful of ordinary Python programs *through the VM* instead of
through CPython's own eval loop, to show it's not just passing unit tests
in isolation -- it's actually executing real control flow, recursion, and
container code end to end."""

from src.vm import VirtualMachine

PROGRAMS = {
    "fibonacci (iterative)": """
def fib(n):
    a, b = 0, 1
    i = 0
    while i < n:
        a, b = b, a + b
        i += 1
    return a

for i in range(10):
    print(i, '->', fib(i))
""",
    "factorial (recursive)": """
def factorial(n):
    if n <= 1:
        return 1
    return n * factorial(n - 1)

print('10! =', factorial(10))
""",
    "primes under 30 (sieve-ish trial division)": """
def is_prime(n):
    if n < 2:
        return False
    i = 2
    is_p = True
    while i * i <= n:
        if n % i == 0:
            is_p = False
        i += 1
    return is_p

primes = []
for n in range(30):
    if is_prime(n):
        primes = primes + [n]
print('primes under 30:', primes)
""",
    "word-length histogram": """
words = ['pear', 'kiwi', 'fig', 'date', 'plum', 'lime']
counts = {}
for w in words:
    length = len(w)
    if length in counts:
        counts[length] = counts[length] + 1
    else:
        counts[length] = 1
print('lengths:', counts)
""",
}

for title, source in PROGRAMS.items():
    print(f"--- {title} ---")
    VirtualMachine().run(compile(source, title, "exec"))
    print()
