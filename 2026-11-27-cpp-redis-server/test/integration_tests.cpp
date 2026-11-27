// End-to-end tests: start the real server binary on a random port and talk to
// it over TCP, including the awkward cases (pipelining, one byte at a time,
// huge values, garbage, idle clients, many clients at once).
//
// usage: integration_tests ./build/san/server
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "protocol.h"

static const char *g_server_bin = nullptr;

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Runs the server as a child process; reads the port from its first line.
struct ServerProc {
    pid_t pid = -1;
    int port = 0;
    explicit ServerProc(int idle_ms) {
        int pipefd[2];
        if (pipe(pipefd) < 0) abort();
        pid = fork();
        if (pid == 0) {
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[0]);
            close(pipefd[1]);
            std::string idle = std::to_string(idle_ms);
            execl(g_server_bin, g_server_bin, "--port", "0", "--idle-ms", idle.c_str(),
                  static_cast<char *>(nullptr));
            _exit(127);
        }
        close(pipefd[1]);
        std::string line;
        char c;
        while (read(pipefd[0], &c, 1) == 1 && c != '\n') line += c;
        close(pipefd[0]);
        size_t colon = line.rfind(':');
        port = colon == std::string::npos ? 0 : atoi(line.c_str() + colon + 1);
        if (port == 0) {
            fprintf(stderr, "server did not start: '%s'\n", line.c_str());
            exit(1);
        }
    }
    // SIGTERM, then the exit status (0 means a clean shutdown, and under ASan
    // also no leaks).
    int stop() {
        if (pid < 0) return -1;
        kill(pid, SIGTERM);
        int status = 0;
        waitpid(pid, &status, 0);
        pid = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    ~ServerProc() { stop(); }
};

struct Client {
    int fd = -1;
    Buffer in;
    explicit Client(int port) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(uint16_t(port));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0) {
            perror("connect");
            abort();
        }
    }
    ~Client() {
        if (fd >= 0) close(fd);
    }
    bool send_raw(const uint8_t *p, size_t n) {
        while (n > 0) {
            ssize_t rv = ::send(fd, p, n, MSG_NOSIGNAL);
            if (rv < 0 && errno == EINTR) continue;
            if (rv <= 0) return false;
            p += rv;
            n -= size_t(rv);
        }
        return true;
    }
    bool send(const std::vector<std::string> &cmd) {
        Buffer b;
        encode_request(b, cmd);
        return send_raw(b.data(), b.size());
    }
    // Reads one response; returns false on EOF, error or a 5 s timeout.
    bool recv(Value &v) {
        uint8_t buf[64 * 1024];
        while (true) {
            size_t used = 0;
            Parse r = parse_response(in.data(), in.size(), v, used);
            if (r == Parse::Error) return false;
            if (r == Parse::Ok) {
                in.erase(in.begin(), in.begin() + used);
                return true;
            }
            pollfd p{fd, POLLIN, 0};
            if (poll(&p, 1, 5000) <= 0) return false;
            ssize_t rv = read(fd, buf, sizeof buf);
            if (rv <= 0) return false;
            in.insert(in.end(), buf, buf + rv);
        }
    }
    Value call(const std::vector<std::string> &cmd) {
        Value v;
        if (!send(cmd) || !recv(v)) {
            v.tag = TAG_ERR;
            v.s = "<connection failed>";
        }
        return v;
    }
    // True if the server closed the connection within `ms`.
    bool closed_within(int ms) {
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, ms) <= 0) return false;
        char c;
        return read(fd, &c, 1) == 0;
    }
};

TEST(basic_commands_over_tcp) {
    ServerProc srv(5000);
    Client c(srv.port);
    CHECK_EQ(c.call({"ping"}).s, std::string("PONG"));
    CHECK(c.call({"get", "k"}).tag == TAG_NIL);
    CHECK(c.call({"set", "k", "hello"}).tag == TAG_NIL);
    CHECK_EQ(c.call({"get", "k"}).s, std::string("hello"));
    CHECK_EQ(c.call({"del", "k"}).i, int64_t(1));
    Value v = c.call({"bogus"});
    CHECK(v.tag == TAG_ERR && v.code == ERR_UNKNOWN);
    CHECK_EQ(c.call({"ping"}).s, std::string("PONG"));  // errors don't kill the connection
    CHECK_EQ(srv.stop(), 0);
}

TEST(pipelined_requests_answered_in_order) {
    ServerProc srv(5000);
    Client c(srv.port);
    Buffer b;
    const int n = 1000;
    for (int i = 0; i < n; i++) encode_request(b, {"set", "k" + std::to_string(i), std::to_string(i)});
    for (int i = 0; i < n; i++) encode_request(b, {"get", "k" + std::to_string(i)});
    CHECK(c.send_raw(b.data(), b.size()));  // all 2000 in one go
    bool ok = true;
    for (int i = 0; i < n; i++) {
        Value v;
        ok &= c.recv(v) && v.tag == TAG_NIL;
    }
    for (int i = 0; i < n; i++) {
        Value v;
        ok &= c.recv(v) && v.s == std::to_string(i);
    }
    CHECK(ok);
    CHECK_EQ(srv.stop(), 0);
}

TEST(request_split_into_single_bytes) {
    ServerProc srv(5000);
    Client c(srv.port);
    int one = 1;
    setsockopt(c.fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    Buffer b;
    encode_request(b, {"set", "slow", "drip"});
    for (uint8_t byte : b) {
        CHECK(c.send_raw(&byte, 1));
        sleep_ms(1);  // separate segments, so the server sees partial frames
    }
    Value v;
    CHECK(c.recv(v) && v.tag == TAG_NIL);
    CHECK_EQ(c.call({"get", "slow"}).s, std::string("drip"));
    CHECK_EQ(srv.stop(), 0);
}

TEST(large_value_roundtrip) {
    ServerProc srv(5000);
    Client c(srv.port);
    std::string big(20 << 20, '\0');  // 20 MiB: many partial reads and writes
    for (size_t i = 0; i < big.size(); i++) big[i] = char('a' + i % 26);
    CHECK(c.call({"set", "big", big}).tag == TAG_NIL);
    Value v = c.call({"get", "big"});
    CHECK(v.tag == TAG_STR && v.s == big);
    CHECK_EQ(srv.stop(), 0);
}

TEST(oversized_message_closes_connection) {
    ServerProc srv(5000);
    Client c(srv.port);
    Buffer b;
    put_u32(b, uint32_t(k_max_msg + 1));
    CHECK(c.send_raw(b.data(), b.size()));
    CHECK(c.closed_within(2000));
    Client other(srv.port);  // the server itself is fine
    CHECK_EQ(other.call({"ping"}).s, std::string("PONG"));
    CHECK_EQ(srv.stop(), 0);
}

TEST(malformed_frame_closes_connection) {
    ServerProc srv(5000);
    Client c(srv.port);
    Buffer b;
    put_u32(b, 8);
    put_u32(b, 1);
    put_u32(b, 999);  // string longer than the frame
    CHECK(c.send_raw(b.data(), b.size()));
    CHECK(c.closed_within(2000));
    CHECK_EQ(srv.stop(), 0);
}

TEST(idle_connections_are_closed) {
    ServerProc srv(300);
    Client idle(srv.port);
    Client busy(srv.port);
    auto start = std::chrono::steady_clock::now();
    bool busy_ok = true;
    for (int i = 0; i < 8; i++) {  // keep one client chatting for ~800 ms
        busy_ok &= busy.call({"ping"}).s == "PONG";
        sleep_ms(100);
    }
    CHECK(busy_ok);
    CHECK(idle.closed_within(100));  // closed long ago
    auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed >= std::chrono::milliseconds(300));
    CHECK_EQ(busy.call({"ping"}).s, std::string("PONG"));
    CHECK_EQ(srv.stop(), 0);
}

TEST(ttl_expires_keys_without_traffic) {
    ServerProc srv(5000);
    {
        Client c(srv.port);
        for (int i = 0; i < 100; i++) {
            c.call({"set", "t" + std::to_string(i), "v"});
            c.call({"pexpire", "t" + std::to_string(i), "150"});
        }
        c.call({"set", "keep", "v"});
        Value ttl = c.call({"pttl", "t0"});
        CHECK(ttl.tag == TAG_INT && ttl.i > 0 && ttl.i <= 150);
    }
    sleep_ms(400);  // no client connected: only the timer can remove them
    Client c(srv.port);
    Value keys = c.call({"keys"});
    CHECK(keys.arr.size() == 1 && keys.arr[0].s == "keep");
    CHECK_EQ(c.call({"pttl", "t5"}).i, int64_t(-2));
    CHECK_EQ(srv.stop(), 0);
}

TEST(many_concurrent_clients) {
    ServerProc srv(5000);
    const int nclients = 64, rounds = 50;
    std::vector<Client *> clients;
    for (int i = 0; i < nclients; i++) clients.push_back(new Client(srv.port));
    // Interleave: every client has a request in flight at the same time.
    bool ok = true;
    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < nclients; i++) {
            ok &= clients[i]->send({"zadd", "board", std::to_string(r * nclients + i),
                                    "p" + std::to_string(i)});
        }
        for (int i = 0; i < nclients; i++) {
            Value v;
            ok &= clients[i]->recv(v) && v.tag == TAG_INT;
        }
    }
    CHECK(ok);
    Value top = clients[0]->call({"zquery", "board", "0", "", "-1000", "1000"});
    // Seeking to the first member and stepping back runs off the start.
    CHECK_EQ(top.arr.size(), size_t(0));
    top = clients[0]->call({"zquery", "board", "0", "", "0", "1000"});
    CHECK_EQ(top.arr.size(), size_t(nclients * 2));
    if (top.arr.size() == size_t(nclients * 2)) {
        CHECK_EQ(top.arr[0].s, std::string("p0"));  // last round's scores keep p0 lowest
        CHECK_EQ(top.arr[1].d, double((rounds - 1) * nclients));
    }
    CHECK_EQ(clients[5]->call({"zrank", "board", "p5"}).i, int64_t(5));
    for (Client *c : clients) delete c;
    CHECK_EQ(srv.stop(), 0);
}

TEST(client_disconnecting_mid_reply_is_harmless) {
    ServerProc srv(5000);
    {
        Client c(srv.port);
        std::string big(8 << 20, 'z');
        c.call({"set", "big", big});
        for (int i = 0; i < 5; i++) c.send({"get", "big"});  // 40 MiB of replies queued
    }  // close without reading: the server hits EPIPE / ECONNRESET
    sleep_ms(100);
    Client c(srv.port);
    CHECK_EQ(c.call({"ping"}).s, std::string("PONG"));
    CHECK_EQ(srv.stop(), 0);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <server binary>\n", argv[0]);
        return 2;
    }
    g_server_bin = argv[1];
    signal(SIGPIPE, SIG_IGN);
    return run_all_tests();
}
