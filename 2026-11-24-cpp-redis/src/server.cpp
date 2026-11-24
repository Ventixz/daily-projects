// Single-threaded event loop: non-blocking sockets + poll().
//
// One thread serves every client. Each connection is a small state machine
// that either wants to read (waiting for a request) or wants to write (has
// replies queued). poll() tells us which sockets are ready, and its timeout
// is set to the nearest deadline, so idle-connection timers and key TTLs fire
// on time without a separate timer thread.
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
#include "resp.h"
#include "store.h"
#include "thread_pool.h"

namespace {

constexpr size_t kMaxPendingOutput = 64 * 1024 * 1024;

struct Config {
    int port = 6380;
    const char *bind = "127.0.0.1";
    uint64_t idle_timeout_ms = 300 * 1000;
    size_t threads = 4;
};

struct Conn {
    int fd = -1;
    bool want_read = true;
    bool want_write = false;
    bool want_close = false;
    bool close_after_write = false;
    std::string incoming;
    std::string outgoing;
    size_t out_pos = 0;  // bytes of `outgoing` already sent
    uint64_t last_active_ms = 0;
    DList idle_node;  // position in the idle-timeout list
};

volatile sig_atomic_t g_stop = 0;

uint64_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000 + static_cast<uint64_t>(ts.tv_nsec) / 1000000;
}

void die(const char *msg) {
    fprintf(stderr, "[%d] %s: %s\n", errno, msg, strerror(errno));
    exit(1);
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) die("fcntl");
}

class Server {
public:
    explicit Server(const Config &cfg) : cfg_(cfg), pool_(cfg.threads), store_(&pool_) {}

    int listen_or_die() {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) die("socket");
        int on = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(cfg_.port));
        if (inet_pton(AF_INET, cfg_.bind, &addr.sin_addr) != 1) {
            fprintf(stderr, "bad bind address: %s\n", cfg_.bind);
            exit(2);
        }
        if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) die("bind");
        if (listen(listen_fd_, SOMAXCONN) < 0) die("listen");
        set_nonblocking(listen_fd_);
        socklen_t len = sizeof(addr);
        getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&addr), &len);
        return ntohs(addr.sin_port);
    }

    void run() {
        std::vector<pollfd> pfds;
        while (!g_stop) {
            pfds.clear();
            pfds.push_back({listen_fd_, POLLIN, 0});
            for (Conn *conn : fd2conn_) {
                if (!conn) continue;
                short events = POLLERR;
                if (conn->want_read) events |= POLLIN;
                if (conn->want_write) events |= POLLOUT;
                pfds.push_back({conn->fd, events, 0});
            }
            int rv = poll(pfds.data(), pfds.size(), next_timeout_ms());
            if (rv < 0 && errno == EINTR) continue;
            if (rv < 0) die("poll");

            if (pfds[0].revents) accept_all();
            for (size_t i = 1; i < pfds.size(); i++) {
                short ready = pfds[i].revents;
                if (!ready) continue;
                Conn *conn = fd2conn_[pfds[i].fd];
                touch(conn);
                if (ready & POLLIN) handle_read(conn);
                if ((ready & POLLOUT) && !conn->want_close) handle_write(conn);
                if ((ready & POLLERR) || conn->want_close) destroy(conn);
            }
            process_timers();
        }
        for (Conn *conn : fd2conn_) {
            if (conn) destroy(conn);
        }
        close(listen_fd_);
    }

private:
    int next_timeout_ms() {
        uint64_t now = now_ms();
        uint64_t next = store_.next_expiry();
        if (!dlist_empty(&idle_list_)) {
            Conn *conn = conn_of(idle_list_.next);
            next = std::min(next, conn->last_active_ms + cfg_.idle_timeout_ms);
        }
        if (next == UINT64_MAX) return -1;  // nothing scheduled: sleep until I/O
        if (next <= now) return 0;
        return static_cast<int>(std::min<uint64_t>(next - now, 60 * 1000));
    }

    static Conn *conn_of(DList *node) {
        // container_of(): step back from the embedded list node to its Conn.
        return reinterpret_cast<Conn *>(reinterpret_cast<char *>(node) - kIdleNodeOffset);
    }

    void touch(Conn *conn) {
        conn->last_active_ms = now_ms();
        dlist_detach(&conn->idle_node);
        dlist_insert_before(&idle_list_, &conn->idle_node);  // newest at the tail
    }

    void process_timers() {
        uint64_t now = now_ms();
        while (!dlist_empty(&idle_list_)) {
            Conn *conn = conn_of(idle_list_.next);
            if (conn->last_active_ms + cfg_.idle_timeout_ms > now) break;  // the rest are newer
            destroy(conn);
        }
        store_.expire_keys(now);
    }

    void accept_all() {
        while (true) {
            sockaddr_in client{};
            socklen_t len = sizeof(client);
            int fd = accept(listen_fd_, reinterpret_cast<sockaddr *>(&client), &len);
            if (fd < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && errno != ECONNABORTED) {
                    perror("accept");
                }
                return;
            }
            set_nonblocking(fd);
            int on = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
            auto *conn = new Conn;
            conn->fd = fd;
            if (fd2conn_.size() <= static_cast<size_t>(fd)) fd2conn_.resize(fd + 1, nullptr);
            fd2conn_[fd] = conn;
            touch(conn);
        }
    }

    void destroy(Conn *conn) {
        close(conn->fd);
        fd2conn_[conn->fd] = nullptr;
        dlist_detach(&conn->idle_node);
        delete conn;
    }

    // Parse and run every complete request in the buffer. Pipelined requests
    // (many sent before reading any reply) are all answered in one batch.
    void process_requests(Conn *conn) {
        size_t pos = 0;
        std::vector<std::string> args;
        while (!conn->close_after_write) {
            size_t consumed = 0;
            std::string err;
            std::string_view rest(conn->incoming.data() + pos, conn->incoming.size() - pos);
            ParseResult r = parse_request(rest, args, &consumed, &err);
            if (r == ParseResult::kIncomplete) break;
            if (r == ParseResult::kError) {
                Reply(conn->outgoing).error("ERR " + err);
                conn->close_after_write = true;  // can't resync a broken stream
                break;
            }
            pos += consumed;
            if (args.empty()) continue;  // blank inline line
            bool close_after = false;
            store_.execute(args, conn->outgoing, now_ms(), &close_after);
            if (close_after) conn->close_after_write = true;
            if (conn->outgoing.size() > kMaxPendingOutput) {
                conn->want_close = true;  // client sends but never reads
                return;
            }
        }
        conn->incoming.erase(0, pos);  // one erase per batch, not per request
        if (conn->outgoing.size() > conn->out_pos) {
            conn->want_read = false;  // stop reading until replies are flushed
            conn->want_write = true;
        }
    }

    void handle_read(Conn *conn) {
        char buf[64 * 1024];
        ssize_t n = read(conn->fd, buf, sizeof(buf));
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
        if (n <= 0) {  // error or EOF
            conn->want_close = true;
            return;
        }
        conn->incoming.append(buf, static_cast<size_t>(n));
        process_requests(conn);
        // Most replies fit in the socket buffer: try now rather than waiting
        // a whole poll() round trip.
        if (conn->want_write) handle_write(conn);
    }

    void handle_write(Conn *conn) {
        while (conn->out_pos < conn->outgoing.size()) {
            ssize_t n = write(conn->fd, conn->outgoing.data() + conn->out_pos, conn->outgoing.size() - conn->out_pos);
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && errno == EAGAIN) return;  // socket full; poll will call us back
            if (n < 0) {
                conn->want_close = true;
                return;
            }
            conn->out_pos += static_cast<size_t>(n);
        }
        conn->outgoing.clear();
        conn->out_pos = 0;
        if (conn->close_after_write) {
            conn->want_close = true;
            return;
        }
        conn->want_write = false;
        conn->want_read = true;
        // Requests may have arrived while we were writing; they are already
        // buffered, so answer them now instead of waiting for more input.
        if (!conn->incoming.empty()) {
            process_requests(conn);
            if (conn->want_write) handle_write(conn);
        }
    }

    static const size_t kIdleNodeOffset;

    Config cfg_;
    ThreadPool pool_;
    Store store_;
    int listen_fd_ = -1;
    std::vector<Conn *> fd2conn_;
    DList idle_list_;
};

// offsetof on a non-standard-layout type is "conditionally supported"; compute
// the same thing from a real object instead.
const size_t Server::kIdleNodeOffset = [] {
    Conn probe;
    return static_cast<size_t>(reinterpret_cast<char *>(&probe.idle_node) - reinterpret_cast<char *>(&probe));
}();

void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--port N] [--bind ADDR] [--idle-timeout-ms N] [--threads N]\n"
            "  --port 0 picks a free port; the chosen one is printed on stdout.\n",
            argv0);
    exit(2);
}

}  // namespace

int main(int argc, char **argv) {
    Config cfg;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (i + 1 >= argc) usage(argv[0]);
        if (a == "--port") {
            cfg.port = atoi(argv[++i]);
        } else if (a == "--bind") {
            cfg.bind = argv[++i];
        } else if (a == "--idle-timeout-ms") {
            cfg.idle_timeout_ms = strtoull(argv[++i], nullptr, 10);
        } else if (a == "--threads") {
            cfg.threads = strtoull(argv[++i], nullptr, 10);
        } else {
            usage(argv[0]);
        }
    }
    signal(SIGPIPE, SIG_IGN);  // a client vanishing mid-write must not kill us
    struct sigaction sa{};
    sa.sa_handler = [](int) { g_stop = 1; };
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    Server server(cfg);
    int port = server.listen_or_die();
    printf("listening on %s:%d\n", cfg.bind, port);
    fflush(stdout);
    server.run();
    return 0;
}
