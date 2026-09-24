/* The naive server: accept one connection, serve it completely (every
 * command, until QUIT or disconnect), then accept the next. While a
 * connection is being served -- including sitting inside a SLEEP command --
 * no other client can even be accepted, let alone answered. */
#include "common.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : DEFAULT_PORT;
    int lfd = make_listener(port, 16);
    printf("[sequential] listening on port %d\n", port);
    fflush(stdout);

    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        serve_blocking_connection(cfd); /* nobody else is served until this returns */
        close(cfd);
    }
}
