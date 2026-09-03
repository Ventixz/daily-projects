extern void print_int(int x);

int main(void) {
    print_int(1 < 2);
    print_int(2 < 1);
    print_int(2 <= 2);
    print_int(3 >= 4);
    print_int(5 == 5);
    print_int(5 != 5);
    print_int(!0);
    print_int(!5);
    return 0;
}
