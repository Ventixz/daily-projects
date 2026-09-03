extern void print_int(int x);

int main(void) {
    int x;
    x = 1;
    print_int(x);  // 1
    {
        int x;
        x = 2;
        print_int(x);  // 2 (inner shadow)
        {
            int x;
            x = 3;
            print_int(x);  // 3
        }
        print_int(x);  // back to 2
    }
    print_int(x);  // back to 1: outer x was never touched by the inner shadows

    int i;
    for (i = 0; i < 3; i = i + 1) {
        int i;  // shadows the loop counter inside the body -- must not affect the loop
        i = 100;
        print_int(i);  // 100, three times
    }
    print_int(i);  // 3: the real loop counter kept counting independently

    return 0;
}
