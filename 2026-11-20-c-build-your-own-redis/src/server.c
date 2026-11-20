/* Single-threaded, non-blocking Redis-like server built on poll().
 *
 *   redis-server [-p PORT] [-i IDLE_TIMEOUT_MS]
 *
 * One loop handles every client: it reads whatever bytes are available,
 * answers every complete request in the buffer (that is pipelining), writes
 * what the socket will take, and sleeps until the next socket event or the
 * next timer (idle connection or key TTL), whichever comes first. */
#include "buf.h"
#include "common.h"
#include "db.h"
#include "list.h"
#include "proto.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

enum { K_MAX_EXPIRE_WORK = 2000 };

typedef struct {
    int fd;
    bool want_read, want_write, want_close;
    Buf in, out;
    uint64_t last_active_ms;
    DList idle;  /* position in the idle list, oldest first */
} Conn;

typedef struct {
    int listen_fd;
    Conn **fd2conn;  /* indexed by fd */
    size_t fd2conn_cap;
    DList idle_list;
    uint64_t idle_timeout_ms;
    DB db;
} Server;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) { (void)sig; g_stop = 1; }

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) die("fcntl");
}

static void conn_put(Server *s, Conn *c) {
    if ((size_t)c->fd >= s->fd2conn_cap) {
        size_t cap = s->fd2conn_cap ? s->fd2conn_cap : 64;
        while (cap <= (size_t)c->fd) cap *= 2;
        Conn **v = realloc(s->fd2conn, cap * sizeof(Conn *));
        if (!v) die("realloc");
        memset(v + s->fd2conn_cap, 0, (cap - s->fd2conn_cap) * sizeof(Conn *));
        s->fd2conn = v;
        s->fd2conn_cap = cap;
    }
    s->fd2conn[c->fd] = c;
}

static void conn_destroy(Server *s, Conn *c) {
    close(c->fd);
    s->fd2conn[c->fd] = NULL;
    dlist_detach(&c->idle);
    buf_free(&c->in);
    buf_free(&c->out);
    free(c);
}

static void conn_touch(Server *s, Conn *c, uint64_t now) {
    c->last_active_ms = now;
    dlist_detach(&c->idle);
    dlist_insert_before(&s->idle_list, &c->idle);  /* most recent at the tail */
}

static void handle_accept(Server *s, uint64_t now) {
    for (;;) {
        struct sockaddr_in addr;
        socklen_t len = sizeof addr;
        int fd = accept(s->listen_fd, (struct sockaddr *)&addr, &len);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) perror("accept");
            return;
        }
        set_nonblocking(fd);
        Conn *c = calloc(1, sizeof *c);
        if (!c) die("calloc");
        c->fd = fd;
        c->want_read = true;
        dlist_init(&c->idle);
        conn_put(s, c);
        conn_touch(s, c, now);
    }
}

/* Answer one request if a whole one is buffered. Returns false when more bytes
 * are needed or the connection must close. */
static bool try_one_request(Server *s, Conn *c, uint64_t now) {
    if (c->in.len < 4) return false;
    uint32_t len = rd_u32(buf_data(&c->in));
    if (len > K_MAX_MSG) {
        fprintf(stderr, "fd %d: request too long (%u bytes), closing\n", c->fd, len);
        c->want_close = true;
        return false;
    }
    if (c->in.len < 4 + (size_t)len) return false;

    Strs cmd;
    if (parse_request(buf_data(&c->in) + 4, len, &cmd) < 0) {
        fprintf(stderr, "fd %d: malformed request, closing\n", c->fd);
        c->want_close = true;
        return false;
    }
    /* Reserve the length header, write the body, then patch the header. */
    size_t header = c->out.len;
    buf_append_u32(&c->out, 0);
    db_execute(&s->db, &cmd, &c->out, now);
    size_t body = c->out.len - header - 4;
    if (body > K_MAX_MSG) {
        buf_truncate(&c->out, header + 4);
        out_err(&c->out, ERR_TOO_BIG, "response is too big");
        body = c->out.len - header - 4;
    }
    wr_u32(buf_data(&c->out) + header, (uint32_t)body);
    free(cmd.v);
    /* Consume only after executing: the Strs point into c->in. */
    buf_consume(&c->in, 4 + (size_t)len);
    return true;
}

static void handle_write(Conn *c) {
    while (c->out.len > 0) {
        ssize_t rv = write(c->fd, buf_data(&c->out), c->out.len);
        if (rv < 0 && errno == EINTR) continue;
        if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;  /* socket full */
        if (rv < 0) {
            perror("write");
            c->want_close = true;
            return;
        }
        buf_consume(&c->out, (size_t)rv);
    }
    /* Everything flushed: go back to reading. */
    c->want_write = false;
    c->want_read = true;
}

static void handle_read(Server *s, Conn *c, uint64_t now) {
    uint8_t chunk[64 * 1024];
    ssize_t rv = read(c->fd, chunk, sizeof chunk);
    if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
    if (rv < 0) {
        perror("read");
        c->want_close = true;
        return;
    }
    if (rv == 0) {
        if (c->in.len > 0) fprintf(stderr, "fd %d: unexpected EOF mid-request\n", c->fd);
        c->want_close = true;
        return;
    }
    buf_append(&c->in, chunk, (size_t)rv);
    while (try_one_request(s, c, now)) {
    }
    if (c->out.len > 0) {
        /* Stop reading until the replies are out: a client that pipelines
         * forever without reading can't make us buffer unbounded output. */
        c->want_read = false;
        c->want_write = true;
        handle_write(c);  /* the socket is usually writable right away */
    }
}

static int next_timeout_ms(Server *s, uint64_t now) {
    uint64_t next = db_next_expiry(&s->db);
    if (s->idle_timeout_ms && !dlist_empty(&s->idle_list)) {
        Conn *oldest = container_of(s->idle_list.next, Conn, idle);
        uint64_t t = oldest->last_active_ms + s->idle_timeout_ms;
        if (t < next) next = t;
    }
    if (next == UINT64_MAX) return -1;
    if (next <= now) return 0;
    uint64_t wait = next - now;
    return wait > 60000 ? 60000 : (int)wait;
}

static void process_timers(Server *s, uint64_t now) {
    while (s->idle_timeout_ms && !dlist_empty(&s->idle_list)) {
        Conn *c = container_of(s->idle_list.next, Conn, idle);
        if (c->last_active_ms + s->idle_timeout_ms > now) break;  /* list is sorted */
        fprintf(stderr, "fd %d: idle timeout\n", c->fd);
        conn_destroy(s, c);
    }
    db_expire(&s->db, now, K_MAX_EXPIRE_WORK);
}

static int listen_on(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket");
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port)};
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) die("bind");
    if (listen(fd, SOMAXCONN) < 0) die("listen");
    set_nonblocking(fd);
    return fd;
}

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s [-p PORT] [-i IDLE_TIMEOUT_MS]\n  PORT 0 picks a free port\n", argv0);
    exit(2);
}

int main(int argc, char **argv) {
    long port = 1234, idle_ms = 5000;
    for (int i = 1; i < argc; i++) {
        char *end;
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = strtol(argv[++i], &end, 10);
            if (*end || port < 0 || port > 65535) usage(argv[0]);
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            idle_ms = strtol(argv[++i], &end, 10);
            if (*end || idle_ms < 0) usage(argv[0]);
        } else {
            usage(argv[0]);
        }
    }

    signal(SIGPIPE, SIG_IGN);  /* a vanished client must not kill the server */
    struct sigaction sa = {.sa_handler = on_signal};
    sigemptyset(&sa.sa_mask);  /* no SA_RESTART: poll() returns EINTR */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    Server s = {.listen_fd = listen_on((uint16_t)port), .idle_timeout_ms = (uint64_t)idle_ms};
    dlist_init(&s.idle_list);
    db_init(&s.db);

    struct sockaddr_in bound;
    socklen_t blen = sizeof bound;
    getsockname(s.listen_fd, (struct sockaddr *)&bound, &blen);
    printf("listening on 127.0.0.1:%u\n", ntohs(bound.sin_port));
    fflush(stdout);

    struct pollfd *pfds = NULL;
    size_t pfds_cap = 0;
    while (!g_stop) {
        /* Rebuild the poll set every iteration: simple, and O(n) like poll itself. */
        size_t n = 1;
        for (size_t fd = 0; fd < s.fd2conn_cap; fd++) n += s.fd2conn[fd] != NULL;
        if (n > pfds_cap) {
            pfds_cap = n * 2;
            pfds = realloc(pfds, pfds_cap * sizeof *pfds);
            if (!pfds) die("realloc");
        }
        pfds[0] = (struct pollfd){.fd = s.listen_fd, .events = POLLIN};
        size_t k = 1;
        for (size_t fd = 0; fd < s.fd2conn_cap; fd++) {
            Conn *c = s.fd2conn[fd];
            if (!c) continue;
            short ev = POLLERR;
            if (c->want_read) ev |= POLLIN;
            if (c->want_write) ev |= POLLOUT;
            pfds[k++] = (struct pollfd){.fd = c->fd, .events = ev};
        }

        int rv = poll(pfds, (nfds_t)k, next_timeout_ms(&s, monotonic_ms()));
        if (rv < 0 && errno == EINTR) continue;
        if (rv < 0) die("poll");

        uint64_t now = monotonic_ms();
        if (pfds[0].revents) handle_accept(&s, now);
        for (size_t i = 1; i < k; i++) {
            short re = pfds[i].revents;
            if (!re) continue;
            Conn *c = s.fd2conn[pfds[i].fd];
            conn_touch(&s, c, now);
            if (re & POLLIN) handle_read(&s, c, now);
            if ((re & POLLOUT) && c->want_write) handle_write(c);
            if ((re & (POLLERR | POLLHUP)) && !(re & POLLIN)) c->want_close = true;
            if (c->want_close) conn_destroy(&s, c);
        }
        process_timers(&s, monotonic_ms());
    }

    fprintf(stderr, "shutting down\n");
    for (size_t fd = 0; fd < s.fd2conn_cap; fd++) {
        if (s.fd2conn[fd]) conn_destroy(&s, s.fd2conn[fd]);
    }
    free(s.fd2conn);
    free(pfds);
    close(s.listen_fd);
    db_free(&s.db);
    return 0;
}
