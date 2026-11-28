// Single-threaded, non-blocking, poll()-based event loop.
//
// Every socket is non-blocking. Each loop iteration asks poll() which sockets
// are ready, does whatever IO is possible without waiting, and goes back to
// poll(). poll()'s timeout is the nearest timer (idle connection or key TTL),
// so the loop sleeps exactly until there is something to do.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "dlist.h"
#include "protocol.h"
#include "store.h"
#include "thread_pool.h"

namespace {

volatile sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

constexpr size_t k_max_expire_per_loop = 2000;  // bound the time spent on TTLs per iteration

uint64_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000 + uint64_t(ts.tv_nsec) / 1000000;
}

// Byte queue: append at the back, consume from the front without memmove on
// every message (the front is only compacted once it is mostly dead space).
struct Buffer {
    std::string data;
    size_t start = 0;

    const uint8_t *begin() const { return reinterpret_cast<const uint8_t *>(data.data()) + start; }
    size_t size() const { return data.size() - start; }
    void append(const void *p, size_t n) { data.append(static_cast<const char *>(p), n); }
    void consume(size_t n) {
        start += n;
        if (start == data.size()) {
            data.clear();
            start = 0;
        } else if (start > 65536 && start * 2 > data.size()) {
            data.erase(0, start);
            start = 0;
        }
    }
};

struct Conn : DList {  // the DList links it into the idle list
    int fd = -1;
    bool want_read = true;
    bool want_write = false;
    bool want_close = false;
    Buffer incoming;
    Buffer outgoing;
    uint64_t last_active_ms = 0;
};

struct Config {
    std::string host = "127.0.0.1";
    int port = 1234;
    uint64_t idle_timeout_ms = 5000;
    size_t threads = 4;
};

void die(const char *what) {
    std::fprintf(stderr, "server: %s: %s\n", what, std::strerror(errno));
    std::exit(1);
}

bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

class Server {
public:
    explicit Server(const Config &cfg) : cfg_(cfg), pool_(cfg.threads), store_(&pool_) {}
    ~Server() {
        for (Conn *c : fd2conn_) {
            if (c) destroy(c);
        }
        if (listen_fd_ >= 0) close(listen_fd_);
    }

    void listen_on() {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) die("socket");
        int one = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(cfg_.port));
        if (inet_pton(AF_INET, cfg_.host.c_str(), &addr.sin_addr) != 1) {
            std::fprintf(stderr, "server: bad IPv4 address '%s'\n", cfg_.host.c_str());
            std::exit(2);
        }
        if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0) die("bind");
        if (listen(listen_fd_, SOMAXCONN) != 0) die("listen");
        if (!set_nonblocking(listen_fd_)) die("fcntl");
        socklen_t len = sizeof addr;
        getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&addr), &len);
        // Tests start the server on port 0 and read the real port from here.
        std::printf("listening on %s:%u\n", cfg_.host.c_str(), unsigned(ntohs(addr.sin_port)));
        std::fflush(stdout);
    }

    void run() {
        std::vector<pollfd> pfds;
        while (!g_stop) {
            pfds.clear();
            pfds.push_back(pollfd{listen_fd_, POLLIN, 0});
            for (Conn *c : fd2conn_) {
                if (!c) continue;
                short events = POLLERR;
                if (c->want_read) events |= POLLIN;
                if (c->want_write) events |= POLLOUT;
                pfds.push_back(pollfd{c->fd, events, 0});
            }

            int rv = poll(pfds.data(), pfds.size(), next_timeout_ms());
            if (rv < 0) {
                if (errno == EINTR) continue;  // a signal; the loop condition decides
                die("poll");
            }

            if (pfds[0].revents) handle_accept();

            uint64_t now = now_ms();
            for (size_t i = 1; i < pfds.size(); i++) {
                short re = pfds[i].revents;
                if (!re) continue;
                Conn *c = fd2conn_[pfds[i].fd];
                touch(c, now);
                if (re & POLLIN) handle_read(c);
                if (re & POLLOUT) handle_write(c);
                if ((re & (POLLERR | POLLHUP)) || c->want_close) destroy(c);
            }
            process_timers();
        }
    }

private:
    Config cfg_;
    int listen_fd_ = -1;
    std::vector<Conn *> fd2conn_;  // indexed by fd
    DList idle_;                   // least recently active connection first
    ThreadPool pool_;              // declared before store_: the store submits to it
    Store store_;

    void touch(Conn *c, uint64_t now) {
        c->last_active_ms = now;
        c->detach();
        idle_.insert_before(c);
    }

    void handle_accept() {
        while (true) {
            int fd = accept(listen_fd_, nullptr, nullptr);
            if (fd < 0) {
                if (errno == EINTR) continue;
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::fprintf(stderr, "server: accept: %s\n", std::strerror(errno));
                }
                return;  // EMFILE etc: try again on the next iteration
            }
            if (!set_nonblocking(fd)) {
                close(fd);
                continue;
            }
            auto *c = new Conn;
            c->fd = fd;
            touch(c, now_ms());
            if (fd2conn_.size() <= size_t(fd)) fd2conn_.resize(size_t(fd) + 1, nullptr);
            fd2conn_[fd] = c;
        }
    }

    void destroy(Conn *c) {
        close(c->fd);
        fd2conn_[c->fd] = nullptr;
        c->detach();
        delete c;
    }

    // Handles one complete request if the buffer holds one. Returns false when
    // more bytes are needed (or the connection must close).
    bool try_one_request(Conn *c) {
        if (c->incoming.size() < 4) return false;
        const uint8_t *p = c->incoming.begin();
        uint32_t len = 0;
        get_u32(p, p + 4, len);
        if (len > k_max_msg) {
            c->want_close = true;  // the stream cannot be resynchronized
            return false;
        }
        if (c->incoming.size() < 4 + size_t(len)) return false;

        std::vector<std::string> cmd;
        if (!parse_request(p, len, cmd)) {
            c->want_close = true;
            return false;
        }

        std::string &out = c->outgoing.data;
        size_t header = out.size();
        put_u32(out, 0);  // length, patched once the body is written
        store_.execute(cmd, out, now_ms());
        size_t body = out.size() - header - 4;
        if (body > k_max_msg) {
            out.resize(header + 4);
            out_err(out, ERR_TOO_BIG, "response is too big");
            body = out.size() - header - 4;
        }
        patch_u32(out, header, static_cast<uint32_t>(body));

        c->incoming.consume(4 + size_t(len));
        return true;
    }

    void handle_read(Conn *c) {
        uint8_t buf[64 * 1024];
        ssize_t rv = read(c->fd, buf, sizeof buf);
        if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
        if (rv <= 0) {  // error or EOF
            c->want_close = true;
            return;
        }
        c->incoming.append(buf, size_t(rv));
        // Pipelining: a client may send many requests before reading any reply,
        // so handle every complete request that has arrived.
        while (!c->want_close && try_one_request(c)) {
        }
        if (c->outgoing.size() > 0) {
            // Stop reading until the replies are flushed. This is the
            // backpressure that stops one client from growing our buffers forever.
            c->want_read = false;
            c->want_write = true;
            handle_write(c);  // the socket is probably writable right now
        }
    }

    void handle_write(Conn *c) {
        ssize_t rv = send(c->fd, c->outgoing.begin(), c->outgoing.size(), MSG_NOSIGNAL);
        if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
        if (rv < 0) {
            c->want_close = true;
            return;
        }
        c->outgoing.consume(size_t(rv));
        if (c->outgoing.size() == 0) {
            c->want_read = true;
            c->want_write = false;
        }
    }

    int next_timeout_ms() {
        int64_t next = -1;
        if (!idle_.empty()) {
            next = int64_t(static_cast<Conn *>(idle_.next)->last_active_ms + cfg_.idle_timeout_ms);
        }
        int64_t ttl = store_.next_expiry();
        if (ttl >= 0 && (next < 0 || ttl < next)) next = ttl;
        if (next < 0) return -1;  // nothing scheduled: wait for IO forever
        int64_t delta = next - int64_t(now_ms());
        if (delta <= 0) return 0;
        return delta > INT_MAX ? INT_MAX : int(delta);
    }

    void process_timers() {
        uint64_t now = now_ms();
        while (!idle_.empty()) {
            auto *c = static_cast<Conn *>(idle_.next);
            if (c->last_active_ms + cfg_.idle_timeout_ms > now) break;
            destroy(c);
        }
        store_.expire(now, k_max_expire_per_loop);
    }
};

void usage() {
    std::fprintf(stderr,
                 "usage: server [--host IPV4] [--port N] [--idle-timeout-ms N] [--threads N]\n"
                 "  --port 0 picks a free port and prints it\n");
    std::exit(2);
}

long long parse_num(const char *s) {
    char *end = nullptr;
    errno = 0;
    long long v = std::strtoll(s, &end, 10);
    if (errno || *s == '\0' || *end != '\0' || v < 0) usage();
    return v;
}

}  // namespace

int main(int argc, char **argv) {
    Config cfg;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (i + 1 >= argc) usage();
        const char *v = argv[++i];
        if (a == "--host") {
            cfg.host = v;
        } else if (a == "--port") {
            long long p = parse_num(v);
            if (p > 65535) usage();
            cfg.port = int(p);
        } else if (a == "--idle-timeout-ms") {
            cfg.idle_timeout_ms = uint64_t(parse_num(v));
        } else if (a == "--threads") {
            cfg.threads = size_t(parse_num(v));
        } else {
            usage();
        }
    }

    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // no SA_RESTART: poll() must return EINTR so the loop can exit
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);

    Server server(cfg);
    server.listen_on();
    server.run();
    std::fprintf(stderr, "server: shutting down\n");
    return 0;
}
