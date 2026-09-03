extern void print_int(int x);

int gcd(int a, int b) {
    while (b != 0) {
        int t;
        t = b;
        b = a % b;
        a = t;
    }
    return a;
}

int main(void) {
    int i;
    int j;
    int total;
    total = 0;
    for (i = 1; i <= 5; i = i + 1) {
        for (j = 1; j <= 5; j = j + 1) {
            total = total + gcd(i, j);
        }
    }
    print_int(total);
    print_int(gcd(48, 18));
    print_int(gcd(17, 5));
    return 0;
}
