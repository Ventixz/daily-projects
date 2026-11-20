/* Command-line client: sends one command and prints the reply.
 *
 *   redis-cli [-p PORT] COMMAND [ARGS...]
 *   redis-cli set greeting hello      ->  (nil)
 *   redis-cli get greeting            ->  (str) hello */
#include "buf.h"
#include "common.h"
#include "proto.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

static int write_all(int fd, const uint8_t *p, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, p, n);
        if (rv < 0 && errno == EINTR) continue;
        if (rv <= 0) return -1;
        p += rv;
        n -= (size_t)rv;
    }
    return 0;
}

static int read_full(int fd, uint8_t *p, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, p, n);
        if (rv < 0 && errno == EINTR) continue;
        if (rv <= 0) return -1;  /* error or EOF before the reply was complete */
        p += rv;
        n -= (size_t)rv;
    }
    return 0;
}

int main(int argc, char **argv) {
    int port = 1234, first = 1;
    if (argc > 2 && strcmp(argv[1], "-p") == 0) {
        port = atoi(argv[2]);
        first = 3;
    }
    if (first >= argc) {
        fprintf(stderr, "usage: %s [-p PORT] COMMAND [ARGS...]\n", argv[0]);
        return 2;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("connect"); return 1; }

    size_t n = (size_t)(argc - first);
    Str *args = malloc(n * sizeof(Str));
    if (!args) return 1;
    for (size_t i = 0; i < n; i++) args[i] = (Str){argv[first + i], strlen(argv[first + i])};
    Buf req = {0};
    encode_request(&req, args, n);
    free(args);
    if (write_all(fd, buf_data(&req), req.len) < 0) { perror("write"); return 1; }
    buf_free(&req);

    uint8_t hdr[4];
    if (read_full(fd, hdr, 4) < 0) { fprintf(stderr, "connection closed\n"); return 1; }
    uint32_t len = rd_u32(hdr);
    if (len > K_MAX_MSG) { fprintf(stderr, "reply too long\n"); return 1; }
    uint8_t *body = malloc(len ? len : 1);
    if (!body || read_full(fd, body, len) < 0) { fprintf(stderr, "connection closed\n"); return 1; }
    long used = print_value(stdout, body, len);
    free(body);
    close(fd);
    if (used < 0 || (size_t)used != len) { fprintf(stderr, "bad reply\n"); return 1; }
    return 0;
}
