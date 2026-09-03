extern void print_int(int x);

// Exercises every SysV integer argument register this compiler uses
// (rdi, rsi, rdx, rcx, r8, r9) in one call, in an order that would expose
// an argument getting clobbered or landing in the wrong slot.
int combine(int a, int b, int c, int d, int e, int f) {
    return a * 100000 + b * 10000 + c * 1000 + d * 100 + e * 10 + f;
}

int sum6(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

int main(void) {
    print_int(combine(1, 2, 3, 4, 5, 6));
    print_int(sum6(combine(1, 0, 0, 0, 0, 0), 2, 3, 4, 5, 6));  // nested call as an argument
    return 0;
}
