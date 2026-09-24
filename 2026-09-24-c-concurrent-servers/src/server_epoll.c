/* Single thread, one epoll loop, N connections multiplexed on one fd set.
 * No thread ever blocks: sockets are non-blocking, and even SLEEP is
 * implemented as a timerfd registered with the same epoll instance rather
 * than a real sleep() call, because a real sleep() here would freeze every
 * connection this process owns, not just one.
 *
 * This is the file where "shared protocol code, three drivers" earns its
 * keep: linebuf_t and protocol_handle_line() are unchanged from the
 * blocking servers. Everything below is new work needed only because
 * nothing here is allowed to block:
 *   - partial reads:  handled already by linebuf_t, same as the others.
 *   - partial writes: send() can accept fewer bytes than asked, or none
 *     at all (EAGAIN) if the socket's send buffer is full. conn_send()
 *     buffers whatever didn't go out and asks epoll to tell us when the
 *     socket is writable again (EPOLLOUT).
 *   - fd reuse: closing a connection's fd makes that integer available for
 *     reuse by the very next accept(). A timerfd armed for a SLEEP that
 *     outlives its connection must not fire "OK\n" at whatever unrelated
 *     connection happens to have been assigned that same fd number later.
 *     Each connection slot carries a generation counter that a pending
 *     timer captures and re-checks before sending anything.
 */
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

typedef struct {
    int in_use;
    int fd;
    unsigned gen;
    linebuf_t lb;
    char *out;
    size_t out_len, out_sent, out_cap;
    int close_after_flush;
} conn_t;

typedef struct {
    int in_use;
    int client_fd;
    unsigned client_gen;
} timer_entry_t;

static conn_t *conns;
static timer_entry_t *timers;
static int max_fds;
static unsigned next_gen = 1;

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void close_conn(int epfd, conn_t *c) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);
    linebuf_free(&c->lb);
    free(c->out);
    c->out = NULL;
    c->out_len = c->out_sent = c->out_cap = 0;
    c->in_use = 0;
}

static void conn_enqueue(conn_t *c, const char *data, size_t n) {
    if (c->out_sent > 0) {
        /* compact: drop bytes already flushed before growing */
        memmove(c->out, c->out + c->out_sent, c->out_len - c->out_sent);
        c->out_len -= c->out_sent;
        c->out_sent = 0;
    }
    if (c->out_len + n > c->out_cap) {
        size_t newcap = c->out_cap ? c->out_cap * 2 : 256;
        while (newcap < c->out_len + n) newcap *= 2;
        c->out = realloc(c->out, newcap);
        c->out_cap = newcap;
    }
    memcpy(c->out + c->out_len, data, n);
    c->out_len += n;
}

static void conn_try_flush(int epfd, conn_t *c) {
    while (c->out_sent < c->out_len) {
        ssize_t n = send(c->fd, c->out + c->out_sent, c->out_len - c->out_sent, MSG_NOSIGNAL);
        if (n > 0) {
            c->out_sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLOUT;
            ev.data.fd = c->fd;
            epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &ev);
            return;
        }
        if (n < 0 && errno == EINTR) continue;
        close_conn(epfd, c);
        return;
    }
    c->out_len = c->out_sent = 0;
    if (c->close_after_flush) {
        close_conn(epfd, c);
        return;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = c->fd;
    epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &ev);
}

static void conn_send(int epfd, conn_t *c, const char *data, size_t n) {
    if (c->out_len == c->out_sent) { /* nothing already queued: try straight away */
        ssize_t sent = send(c->fd, data, n, MSG_NOSIGNAL);
        if (sent == (ssize_t)n) return;
        if (sent < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                close_conn(epfd, c);
                return;
            }
            sent = 0;
        }
        conn_enqueue(c, data + sent, n - (size_t)sent);
        conn_try_flush(epfd, c);
    } else {
        conn_enqueue(c, data, n);
    }
}

static void start_timer(int epfd, conn_t *c, int ms) {
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (tfd < 0 || tfd >= max_fds) {
        if (tfd >= 0) close(tfd);
        return;
    }
    struct itimerspec its;
    memset(&its, 0, sizeof its);
    its.it_value.tv_sec = ms / 1000;
    its.it_value.tv_nsec = (long)(ms % 1000) * 1000000L;
    timerfd_settime(tfd, 0, &its, NULL);

    timers[tfd].in_use = 1;
    timers[tfd].client_fd = c->fd;
    timers[tfd].client_gen = c->gen;

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = tfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &ev);
}

static void handle_timer(int epfd, int tfd) {
    uint64_t expirations;
    ssize_t rd = read(tfd, &expirations, sizeof expirations);
    (void)rd;

    timer_entry_t t = timers[tfd];
    timers[tfd].in_use = 0;
    epoll_ctl(epfd, EPOLL_CTL_DEL, tfd, NULL);
    close(tfd);

    if (t.client_fd >= 0 && t.client_fd < max_fds && conns[t.client_fd].in_use &&
        conns[t.client_fd].gen == t.client_gen) {
        conn_send(epfd, &conns[t.client_fd], "OK\n", 3);
    }
    /* else: the connection this SLEEP belonged to is long gone -- drop it. */
}

static void handle_readable(int epfd, conn_t *c) {
    char buf[4096];
    ssize_t n = recv(c->fd, buf, sizeof buf, 0);
    if (n == 0) {
        close_conn(epfd, c);
        return;
    }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
        close_conn(epfd, c);
        return;
    }
    linebuf_append(&c->lb, buf, (size_t)n);

    char *line;
    while ((line = linebuf_take_line(&c->lb)) != NULL) {
        char *out;
        int should_close, sleep_ms;
        protocol_handle_line(line, &out, &should_close, &sleep_ms);
        free(line);

        if (sleep_ms > 0) {
            start_timer(epfd, c, sleep_ms);
        } else if (out) {
            conn_send(epfd, c, out, strlen(out));
            free(out);
        }

        if (!c->in_use) return; /* conn_send closed it on a hard error */

        if (should_close) {
            if (c->out_len == c->out_sent) {
                close_conn(epfd, c);
            } else {
                c->close_after_flush = 1;
            }
            return;
        }
    }
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : DEFAULT_PORT;

    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    max_fds = (int)rl.rlim_cur;
    conns = calloc((size_t)max_fds, sizeof *conns);
    timers = calloc((size_t)max_fds, sizeof *timers);

    int lfd = make_listener(port, 128);
    set_nonblocking(lfd);

    int epfd = epoll_create1(0);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);

    printf("[epoll] listening on port %d (max_fds=%d)\n", port, max_fds);
    fflush(stdout);

    struct epoll_event events[64];
    for (;;) {
        int n = epoll_wait(epfd, events, 64, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == lfd) {
                int cfd = accept(lfd, NULL, NULL);
                if (cfd < 0 || cfd >= max_fds) {
                    if (cfd >= 0) close(cfd);
                    continue;
                }
                set_nonblocking(cfd);
                conn_t *c = &conns[cfd];
                c->in_use = 1;
                c->fd = cfd;
                c->gen = next_gen++;
                linebuf_init(&c->lb);
                c->out = NULL;
                c->out_len = c->out_sent = c->out_cap = 0;
                c->close_after_flush = 0;

                struct epoll_event cev;
                cev.events = EPOLLIN;
                cev.data.fd = cfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &cev);
                conn_send(epfd, c, "READY\n", 6);
            } else if (fd < max_fds && conns[fd].in_use) {
                conn_t *c = &conns[fd];
                if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                    close_conn(epfd, c);
                    continue;
                }
                if (events[i].events & EPOLLIN) handle_readable(epfd, c);
                if (c->in_use && (events[i].events & EPOLLOUT)) conn_try_flush(epfd, c);
            } else if (fd < max_fds && timers[fd].in_use) {
                handle_timer(epfd, fd);
            }
        }
    }
    return 0;
}
