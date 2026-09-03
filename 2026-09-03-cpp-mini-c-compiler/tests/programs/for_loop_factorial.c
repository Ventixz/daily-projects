extern void print_int(int x);

int factorial(int n) {
    int result;
    int i;
    result = 1;
    for (i = 2; i <= n; i = i + 1) {
        result = result * i;
    }
    return result;
}

int main(void) {
    print_int(factorial(0));
    print_int(factorial(1));
    print_int(factorial(5));
    print_int(factorial(10));
    return 0;
}
