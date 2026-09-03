extern void print_int(int x);

int main(void) {
    print_int(17 / 5);
    print_int(17 % 5);
    print_int(-17 / 5);   // C truncates toward zero: -3, not -4
    print_int(-17 % 5);   // sign follows the dividend: -2
    print_int(17 / -5);
    print_int(17 % -5);
    return 0;
}
