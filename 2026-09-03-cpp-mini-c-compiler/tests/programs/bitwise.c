extern void print_int(int x);

int main(void) {
    print_int(6 & 3);
    print_int(6 | 3);
    print_int(6 ^ 3);
    print_int(~0);
    print_int(~5);
    return 0;
}
