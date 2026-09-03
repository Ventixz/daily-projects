extern void print_int(int x);

int is_even(int n);  // forward declaration: is_odd below needs to call it before it's defined

int is_odd(int n) {
    if (n == 0) {
        return 0;
    }
    return is_even(n - 1);
}

int is_even(int n) {
    if (n == 0) {
        return 1;
    }
    return is_odd(n - 1);
}

int main(void) {
    print_int(is_even(0));
    print_int(is_even(7));
    print_int(is_even(10));
    print_int(is_odd(7));
    print_int(is_odd(10));
    return 0;
}
