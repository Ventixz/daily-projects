extern void print_int(int x);

int fib(int n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    int i;
    for (i = 0; i < 10; i = i + 1) {
        print_int(fib(i));
    }
    return 0;
}
