extern void print_int(int x);

int main(void) {
    int i;
    int sum;
    i = 1;
    sum = 0;
    while (i <= 10) {
        sum = sum + i;
        i = i + 1;
    }
    print_int(sum);  // 55
    return 0;
}
