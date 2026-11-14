/* target.c - a tiny program for the tests (and for trying minidbg by hand). */
#include <stdio.h>

long counter = 0;

__attribute__((noinline)) long add(long a, long b) { return a + b; }

int main(void) {
    for (long i = 1; i <= 3; ++i) counter = add(counter, i);
    printf("counter = %ld\n", counter);
    return (int)counter; /* 6 unless someone poked the registers or memory */
}
