extern void print_int(int x);

int main(void) {
    int i;
    int sum;
    sum = 0;
    for (i = 0; i < 20; i = i + 1) {
        if (i == 10) {
            break;
        }
        if (i % 2 == 0) {
            continue;  // must run the for-loop's post-expression (i = i + 1), not skip it
        }
        sum = sum + i;
    }
    print_int(sum);  // 1+3+5+7+9 = 25
    print_int(i);    // loop broke out at i == 10

    int j;
    int count;
    j = 0;
    count = 0;
    while (j < 100) {
        j = j + 1;
        if (j % 3 != 0) {
            continue;
        }
        count = count + 1;
        if (count == 5) {
            break;
        }
    }
    print_int(j);      // 15 (5th multiple of 3)
    print_int(count);  // 5
    return 0;
}
