/* Same protocol, same serve_blocking_connection() loop as the sequential
 * server -- the only difference is that accept() hands each connection to
 * its own detached thread instead of serving it inline. A SLEEP command
 * now only blocks the one thread (and the one OS thread stack) handling
 * that connection; the accept loop keeps running and other threads keep
 * answering their own clients. */
#include "common.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

static void *client_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    serve_blocking_connection(fd);
    close(fd);
    return NULL;
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : DEFAULT_PORT;
    int lfd = make_listener(port, 16);
    printf("[threaded] listening on port %d\n", port);
    fflush(stdout);

    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, (void *)(intptr_t)cfd) != 0) {
            perror("pthread_create");
            close(cfd);
            continue;
        }
        pthread_detach(tid);
    }
}
