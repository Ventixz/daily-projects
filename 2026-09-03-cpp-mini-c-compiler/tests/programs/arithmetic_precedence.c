extern void print_int(int x);

int main(void) {
    print_int(1 + 2 * 3);
    print_int((1 + 2) * 3);
    print_int(10 - 3 - 2);   // left-associative: (10 - 3) - 2 = 5, not 10 - (3 - 2) = 9
    print_int(2 + 3 * 4 - 5 / 5);
    print_int(-5 + 3);
    print_int(-(2 + 3));
    return 0;
}
