// Single-threaded, non-blocking key-value server built around poll().
//
// One loop does everything: wait for sockets or the next timer, read what has
// arrived, run every complete request in the buffer, write responses, then
// close idle connections and expire keys. There is no thread per client;
// the only other threads belong to the pool that frees huge sorted sets.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "list.h"
#include "protocol.h"
#include "store.h"
#include "thread_pool.h"

static uint64_t monotonic_ms() {
    timespec tv{};
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return uint64_t(tv.tv_sec) * 1000 + uint64_t(tv.tv_nsec) / 1000000;
}

static void die(const char *msg) {
    fprintf(stderr, "[%d] %s: %s\n", errno, msg, strerror(errno));
    exit(1);
}

static void fd_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) die("fcntl");
}

struct Conn : DList {  // the DList links it into the idle list
    int fd = -1;
    bool want_read = true;
    bool want_write = false;
    bool want_close = false;
    Buffer incoming;
    Buffer outgoing;
    size_t out_pos = 0;  // bytes of `outgoing` already sent
    uint64_t last_active_ms = 0;
};

struct Server {
    int listen_fd = -1;
    uint64_t idle_timeout_ms = 5000;
    std::vector<Conn *> fd2conn;  // indexed by fd
    DList idle_list;              // least recently active first
    ThreadPool pool{4};
    Store store{&pool};
    std::vector<std::string> args;  // reused between requests
};

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int) { g_stop = 1; }

static void conn_touch(Server &srv, Conn *conn, uint64_t now) {
    conn->last_active_ms = now;
    dlist_detach(conn);
    dlist_insert_before(&srv.idle_list, conn);  // move to the tail
}

static void conn_destroy(Server &srv, Conn *conn) {
    close(conn->fd);
    srv.fd2conn[conn->fd] = nullptr;
    dlist_detach(conn);
    delete conn;
}

static void handle_accept(Server &srv, uint64_t now) {
    while (true) {
        sockaddr_in addr{};
        socklen_t len = sizeof addr;
        int fd = accept(srv.listen_fd, reinterpret_cast<sockaddr *>(&addr), &len);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) perror("accept");
            return;
        }
        fd_set_nonblock(fd);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        Conn *conn = new Conn;
        conn->fd = fd;
        if (srv.fd2conn.size() <= size_t(fd)) srv.fd2conn.resize(fd + 1, nullptr);
        srv.fd2conn[fd] = conn;
        conn_touch(srv, conn, now);
    }
}

// Runs every complete request at the front of `incoming`. Returns the number
// of bytes consumed. A malformed request marks the connection for closing:
// once framing is lost there is no way to find the next message boundary.
static size_t process_requests(Server &srv, Conn *conn, uint64_t now) {
    size_t pos = 0;
    while (!conn->want_close) {
        size_t consumed = 0;
        Parse r = parse_request(conn->incoming.data() + pos, conn->incoming.size() - pos,
                                srv.args, consumed);
        if (r == Parse::Incomplete) break;
        if (r == Parse::Error) {
            conn->want_close = true;
            break;
        }
        size_t header = response_begin(conn->outgoing);
        srv.store.execute(srv.args, conn->outgoing, now);
        response_end(conn->outgoing, header);
        pos += consumed;
    }
    return pos;
}

static void handle_write(Conn *conn) {
    while (conn->out_pos < conn->outgoing.size()) {
        ssize_t rv = write(conn->fd, conn->outgoing.data() + conn->out_pos,
                           conn->outgoing.size() - conn->out_pos);
        if (rv < 0 && errno == EINTR) continue;
        if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;  // kernel buffer full
        if (rv <= 0) {
            conn->want_close = true;
            return;
        }
        conn->out_pos += size_t(rv);
    }
    // Everything sent: go back to reading.
    conn->outgoing.clear();
    conn->out_pos = 0;
    conn->want_read = true;
    conn->want_write = false;
}

static void handle_read(Server &srv, Conn *conn, uint64_t now) {
    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof buf);
    if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
    if (rv <= 0) {  // error, or EOF from the peer
        conn->want_close = true;
        return;
    }
    conn->incoming.insert(conn->incoming.end(), buf, buf + rv);
    size_t used = process_requests(srv, conn, now);
    conn->incoming.erase(conn->incoming.begin(), conn->incoming.begin() + used);

    if (!conn->outgoing.empty()) {
        // Stop reading until the replies are out. A client that pipelines
        // faster than it reads gets throttled by TCP instead of making the
        // server buffer without limit.
        conn->want_read = false;
        conn->want_write = true;
        handle_write(conn);  // most of the time the socket is writable already
    }
}

static int next_timeout_ms(Server &srv, uint64_t now) {
    uint64_t next = UINT64_MAX;
    if (!dlist_empty(&srv.idle_list)) {
        Conn *conn = static_cast<Conn *>(srv.idle_list.next);
        next = conn->last_active_ms + srv.idle_timeout_ms;
    }
    uint64_t ttl = srv.store.next_deadline();
    if (ttl < next) next = ttl;
    if (next == UINT64_MAX) return -1;  // nothing scheduled: wait for I/O only
    if (next <= now) return 0;
    uint64_t wait = next - now;
    return wait > 60000 ? 60000 : int(wait);
}

static void process_timers(Server &srv, uint64_t now) {
    while (!dlist_empty(&srv.idle_list)) {
        Conn *conn = static_cast<Conn *>(srv.idle_list.next);
        if (conn->last_active_ms + srv.idle_timeout_ms > now) break;  // the rest are newer
        conn_destroy(srv, conn);
    }
    srv.store.expire(now);
}

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s [--host ADDR] [--port N] [--idle-ms N]\n", prog);
    exit(2);
}

int main(int argc, char **argv) {
    std::string host = "127.0.0.1";
    int port = 1234;
    Server srv;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (i + 1 >= argc) usage(argv[0]);
        if (a == "--host") host = argv[++i];
        else if (a == "--port") port = atoi(argv[++i]);
        else if (a == "--idle-ms") srv.idle_timeout_ms = strtoull(argv[++i], nullptr, 10);
        else usage(argv[0]);
    }

    signal(SIGPIPE, SIG_IGN);  // a closed peer must not kill the server
    struct sigaction sa {};
    sa.sa_handler = on_signal;  // no SA_RESTART: poll() returns EINTR
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    srv.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv.listen_fd < 0) die("socket");
    int one = 1;
    setsockopt(srv.listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(uint16_t(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        fprintf(stderr, "bad --host %s\n", host.c_str());
        return 2;
    }
    if (bind(srv.listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0) die("bind");
    if (listen(srv.listen_fd, SOMAXCONN) < 0) die("listen");
    fd_set_nonblock(srv.listen_fd);

    socklen_t alen = sizeof addr;
    getsockname(srv.listen_fd, reinterpret_cast<sockaddr *>(&addr), &alen);
    printf("listening on %s:%d\n", host.c_str(), ntohs(addr.sin_port));
    fflush(stdout);

    std::vector<pollfd> pfds;
    while (!g_stop) {
        pfds.clear();
        pfds.push_back({srv.listen_fd, POLLIN, 0});
        for (Conn *conn : srv.fd2conn) {
            if (!conn) continue;
            short events = POLLERR;
            if (conn->want_read) events |= POLLIN;
            if (conn->want_write) events |= POLLOUT;
            pfds.push_back({conn->fd, events, 0});
        }

        int rv = poll(pfds.data(), nfds_t(pfds.size()), next_timeout_ms(srv, monotonic_ms()));
        if (rv < 0 && errno == EINTR) continue;
        if (rv < 0) die("poll");

        uint64_t now = monotonic_ms();
        if (pfds[0].revents) handle_accept(srv, now);
        for (size_t i = 1; i < pfds.size(); i++) {
            short ready = pfds[i].revents;
            if (!ready) continue;
            Conn *conn = srv.fd2conn[pfds[i].fd];
            conn_touch(srv, conn, now);
            if (ready & POLLIN) handle_read(srv, conn, now);
            if ((ready & POLLOUT) && !conn->want_close) handle_write(conn);
            bool hangup = (ready & POLLHUP) && !(ready & POLLIN);
            if ((ready & POLLERR) || hangup || conn->want_close) conn_destroy(srv, conn);
        }
        process_timers(srv, monotonic_ms());
    }

    for (Conn *conn : srv.fd2conn) {
        if (conn) conn_destroy(srv, conn);
    }
    close(srv.listen_fd);
    fprintf(stderr, "shutting down\n");
    return 0;  // Store, then ThreadPool, are destroyed here
}
