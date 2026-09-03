// This compiler only routes the 6 SysV integer-argument registers -- a 7th
// parameter is a real limitation (documented in LEARNING.md), not a bug.
int seven(int a, int b, int c, int d, int e, int f, int g) {
    return a + b + c + d + e + f + g;
}

int main(void) {
    return seven(1, 2, 3, 4, 5, 6, 7);
}
