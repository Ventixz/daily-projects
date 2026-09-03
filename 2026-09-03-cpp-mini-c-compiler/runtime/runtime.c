// Tiny runtime linked into every compiled test program. The compiler itself
// emits no I/O of any kind, so anything a mini-C program wants to print has to
// go through here -- and because this is real C compiled by a real compiler,
// the same `extern void print_int(int);` prototype resolves identically
// whether the calling code was assembled by mcc or compiled straight by gcc.
#include <stdio.h>

void print_int(int x) {
    printf("%d\n", x);
}
