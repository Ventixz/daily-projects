#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Above this many buffered bytes with still no '\n', give up on the line
 * rather than growing forever -- a defense against a client that never
 * sends a newline. */
#define MAX_BUFFERED_LINE (1u << 20)

void linebuf_init(linebuf_t *lb) {
    lb->data = NULL;
    lb->len = 0;
    lb->cap = 0;
}

void linebuf_free(linebuf_t *lb) {
    free(lb->data);
    lb->data = NULL;
    lb->len = 0;
    lb->cap = 0;
}

void linebuf_append(linebuf_t *lb, const char *data, size_t n) {
    if (lb->len + n > lb->cap) {
        size_t newcap = lb->cap ? lb->cap * 2 : 256;
        while (newcap < lb->len + n) newcap *= 2;
        lb->data = realloc(lb->data, newcap);
        lb->cap = newcap;
    }
    memcpy(lb->data + lb->len, data, n);
    lb->len += n;
}

char *linebuf_take_line(linebuf_t *lb) {
    char *nl = memchr(lb->data, '\n', lb->len);
    if (!nl) {
        if (lb->len > MAX_BUFFERED_LINE) {
            /* Pathological client: treat everything buffered so far as a
             * (doomed) line so the caller can reject it and move on
             * instead of growing this buffer without bound. */
            size_t linelen = lb->len;
            char *line = malloc(linelen + 1);
            memcpy(line, lb->data, linelen);
            line[linelen] = '\0';
            lb->len = 0;
            return line;
        }
        return NULL;
    }

    size_t linelen = (size_t)(nl - lb->data);
    char *line = malloc(linelen + 1);
    memcpy(line, lb->data, linelen);
    line[linelen] = '\0';
    if (linelen > 0 && line[linelen - 1] == '\r') {
        line[linelen - 1] = '\0';
    }

    size_t consumed = linelen + 1;
    size_t remaining = lb->len - consumed;
    memmove(lb->data, lb->data + consumed, remaining);
    lb->len = remaining;
    return line;
}

int make_listener(int port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind");
        exit(1);
    }
    if (listen(fd, backlog) < 0) {
        perror("listen");
        exit(1);
    }
    return fd;
}

static char *dup_with_newline(const char *s) {
    size_t n = strlen(s);
    char *r = malloc(n + 2);
    memcpy(r, s, n);
    r[n] = '\n';
    r[n + 1] = '\0';
    return r;
}

void protocol_handle_line(const char *line, char **out, int *close, int *sleep_ms) {
    *out = NULL;
    *close = 0;
    *sleep_ms = 0;

    if (strncmp(line, "ECHO ", 5) == 0) {
        *out = dup_with_newline(line + 5);
    } else if (strncmp(line, "REV ", 4) == 0) {
        const char *s = line + 4;
        size_t n = strlen(s);
        char *r = malloc(n + 2);
        for (size_t i = 0; i < n; i++) r[i] = s[n - 1 - i];
        r[n] = '\n';
        r[n + 1] = '\0';
        *out = r;
    } else if (strncmp(line, "UPPER ", 6) == 0) {
        char *r = strdup(line + 6);
        for (char *p = r; *p; p++) *p = (char)toupper((unsigned char)*p);
        *out = dup_with_newline(r);
        free(r);
    } else if (strncmp(line, "SLEEP ", 6) == 0) {
        int ms = atoi(line + 6);
        if (ms < 0) ms = 0;
        if (ms > 10000) ms = 10000; /* cap so a bad client can't wedge a slot forever */
        *sleep_ms = ms;
    } else if (strcmp(line, "QUIT") == 0) {
        *out = strdup("BYE\n");
        *close = 1;
    } else {
        *out = strdup("ERR unknown command\n");
    }
}

void send_all(int fd, const char *data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t k = send(fd, data + sent, n - sent, MSG_NOSIGNAL);
        if (k < 0) {
            if (errno == EINTR) continue;
            return; /* peer gone; nothing more we can do */
        }
        sent += (size_t)k;
    }
}

void serve_blocking_connection(int fd) {
    send_all(fd, "READY\n", 6);

    linebuf_t lb;
    linebuf_init(&lb);
    char buf[4096];

    for (;;) {
        char *line;
        while ((line = linebuf_take_line(&lb)) != NULL) {
            char *out;
            int should_close, sleep_ms;
            protocol_handle_line(line, &out, &should_close, &sleep_ms);
            free(line);

            if (sleep_ms > 0) {
                usleep((useconds_t)sleep_ms * 1000);
                send_all(fd, "OK\n", 3);
            } else if (out) {
                send_all(fd, out, strlen(out));
                free(out);
            }

            if (should_close) {
                linebuf_free(&lb);
                return;
            }
        }

        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) {
            linebuf_free(&lb);
            return;
        }
        linebuf_append(&lb, buf, (size_t)n);
    }
}
