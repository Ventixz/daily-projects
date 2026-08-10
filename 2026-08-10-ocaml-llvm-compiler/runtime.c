/* The only thing Kal programs can't express themselves: talking to the
 * outside world. Declared `extern print_int(x);` on the Kal side and
 * linked in as a native object file alongside each compiled program. */
#include <stdio.h>

int print_int(int x) {
    printf("%d\n", x);
    return 0;
}
