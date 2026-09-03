extern void print_int(int x);

int classify(int n) {
    if (n < 0) {
        return -1;
    } else if (n == 0) {
        return 0;
    } else if (n < 10) {
        return 1;
    } else {
        return 2;
    }
}

int main(void) {
    print_int(classify(-5));
    print_int(classify(0));
    print_int(classify(3));
    print_int(classify(100));
    return 0;
}
