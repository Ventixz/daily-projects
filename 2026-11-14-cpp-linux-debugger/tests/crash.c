/* crash.c - dereferences NULL, to test that minidbg reports signals. */
int main(void) {
    volatile int* p = 0;
    return *p;
}
