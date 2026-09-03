extern void print_int(int x);

int loud_zero(void) {
    print_int(900);  // side effect that should NOT run when short-circuited away
    return 0;
}

int loud_one(void) {
    print_int(901);
    return 1;
}

int main(void) {
    print_int(0 && loud_one());   // false && X must not evaluate X: no "901"
    print_int(1 || loud_zero());  // true || X must not evaluate X: no "900"
    print_int(1 && loud_one());   // true && X evaluates X: prints 901, then 1
    print_int(0 || loud_zero());  // false || X evaluates X: prints 900, then 0
    return 0;
}
