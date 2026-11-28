// End-to-end tests: start the real server binary on a free port and talk to it
// over TCP. The path comes from $SERVER_BIN (set by `make test`).
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <thread>
#include <vector>

#include "protocol.h"
#include "testing.h"

namespace {

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

class ServerProcess {
public:
    explicit ServerProcess(std::vector<std::string> extra = {}) {
        const char *bin = std::getenv("SERVER_BIN");
        if (!bin) throw testing::Failure{"SERVER_BIN is not set; run via `make test`"};
        int out[2];
        if (pipe(out) != 0) throw testing::Failure{"pipe failed"};
        pid_ = fork();
        if (pid_ == 0) {
            dup2(out[1], STDOUT_FILENO);
            close(out[0]);
            close(out[1]);
            std::vector<std::string> args{bin, "--port", "0"};
            args.insert(args.end(), extra.begin(), extra.end());
            std::vector<char *> argv;
            for (std::string &a : args) argv.push_back(a.data());
            argv.push_back(nullptr);
            execv(bin, argv.data());
            _exit(127);
        }
        close(out[1]);
        // First stdout line is "listening on 127.0.0.1:PORT".
        std::string line;
        char ch;
        while (read(out[0], &ch, 1) == 1 && ch != '\n') line += ch;
        close(out[0]);
        size_t colon = line.rfind(':');
        if (line.rfind("listening on", 0) != 0 || colon == std::string::npos) {
            throw testing::Failure{"server did not start: '" + line + "'"};
        }
        port_ = std::atoi(line.c_str() + colon + 1);
    }

    ~ServerProcess() {
        if (pid_ > 0) {
            kill(pid_, SIGKILL);
            waitpid(pid_, nullptr, 0);
        }
    }

    // Sends SIGTERM and returns the exit status (0 = clean shutdown).
    int terminate() {
        kill(pid_, SIGTERM);
        int status = 0;
        waitpid(pid_, &status, 0);
        pid_ = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }

    int port() const { return port_; }

private:
    pid_t pid_ = -1;
    int port_ = 0;
};

class Client {
public:
    explicit Client(int port) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(uint16_t(port));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0) {
            throw testing::Failure{std::string("connect: ") + std::strerror(errno)};
        }
        timeval tv{5, 0};  // a hung server fails the test instead of hanging it
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    ~Client() { close(fd_); }
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    void send_raw(const std::string &bytes) {
        size_t off = 0;
        while (off < bytes.size()) {
            ssize_t rv = ::send(fd_, bytes.data() + off, bytes.size() - off, MSG_NOSIGNAL);
            if (rv <= 0) throw testing::Failure{"send failed"};
            off += size_t(rv);
        }
    }

    // Reads exactly n bytes. Returns false on EOF or error.
    bool read_exact(std::string &buf, size_t n) {
        buf.resize(n);
        size_t off = 0;
        while (off < n) {
            ssize_t rv = ::read(fd_, buf.data() + off, n - off);
            if (rv <= 0) return false;
            off += size_t(rv);
        }
        return true;
    }

    Value read_reply() {
        std::string hdr, body;
        if (!read_exact(hdr, 4)) throw testing::Failure{"connection closed before reply"};
        const auto *p = reinterpret_cast<const uint8_t *>(hdr.data());
        uint32_t len = 0;
        get_u32(p, p + 4, len);
        if (!read_exact(body, len)) throw testing::Failure{"truncated reply"};
        Value v;
        if (!decode_response(reinterpret_cast<const uint8_t *>(body.data()), len, v)) {
            throw testing::Failure{"undecodable reply"};
        }
        return v;
    }

    Value cmd(const std::vector<std::string> &args) {
        send_raw(encode_request(args));
        return read_reply();
    }
    std::string text(const std::vector<std::string> &args) { return format_value(cmd(args)); }

    // True if the server closed the connection (read returns EOF/reset).
    bool closed_by_peer() {
        char c;
        ssize_t rv = ::read(fd_, &c, 1);
        return rv == 0 || (rv < 0 && errno == ECONNRESET);
    }

private:
    int fd_ = -1;
};

}  // namespace

TEST(server_basic_commands) {
    ServerProcess srv;
    Client c(srv.port());
    CHECK_EQ(c.text({"ping"}), std::string("(str) PONG\n"));
    CHECK_EQ(c.text({"set", "greeting", "hello world"}), std::string("(nil)\n"));
    CHECK_EQ(c.text({"get", "greeting"}), std::string("(str) hello world\n"));
    CHECK_EQ(c.text({"del", "greeting"}), std::string("(int) 1\n"));
    CHECK_EQ(c.text({"get", "greeting"}), std::string("(nil)\n"));
    CHECK_EQ(c.cmd({"bogus"}).code, uint32_t(ERR_UNKNOWN));
    CHECK_EQ(srv.terminate(), 0);  // SIGTERM is a clean exit
}

TEST(server_pipelined_requests_answered_in_order) {
    ServerProcess srv;
    Client c(srv.port());
    std::string batch;
    for (int i = 0; i < 500; i++) {
        batch += encode_request({"set", "k" + std::to_string(i), std::to_string(i * i)});
        batch += encode_request({"get", "k" + std::to_string(i)});
    }
    c.send_raw(batch);  // one write, 1000 requests
    for (int i = 0; i < 500; i++) {
        CHECK(c.read_reply().tag == TAG_NIL);
        CHECK_EQ(c.read_reply().s, std::to_string(i * i));
    }
}

TEST(server_handles_requests_split_across_reads) {
    ServerProcess srv;
    Client c(srv.port());
    std::string req = encode_request({"set", "slow", "dripfed"});
    for (char ch : req) {  // one byte per TCP segment, give or take Nagle
        c.send_raw(std::string(1, ch));
        sleep_ms(2);
    }
    CHECK(c.read_reply().tag == TAG_NIL);
    CHECK_EQ(c.text({"get", "slow"}), std::string("(str) dripfed\n"));
}

TEST(server_large_value_round_trip) {
    ServerProcess srv;
    Client c(srv.port());
    std::string big(20u << 20, '\0');  // 20 MiB: many partial reads and writes
    for (size_t i = 0; i < big.size(); i++) big[i] = char('a' + i % 26);
    CHECK(c.cmd({"set", "big", big}).tag == TAG_NIL);
    Value v = c.cmd({"get", "big"});
    CHECK(v.tag == TAG_STR);
    CHECK(v.s == big);
}

TEST(server_oversized_request_closes_only_that_connection) {
    ServerProcess srv;
    Client bad(srv.port());
    Client good(srv.port());
    std::string hdr;
    put_u32(hdr, uint32_t(k_max_msg + 1));
    bad.send_raw(hdr);
    CHECK(bad.closed_by_peer());
    CHECK_EQ(good.text({"ping"}), std::string("(str) PONG\n"));
}

TEST(server_malformed_request_closes_connection) {
    ServerProcess srv;
    Client c(srv.port());
    std::string body;
    put_u32(body, 3);  // claims 3 strings
    put_u32(body, 4);
    body += "ping";  // but only sends 1
    std::string req;
    put_u32(req, uint32_t(body.size()));
    c.send_raw(req + body);
    CHECK(c.closed_by_peer());
}

TEST(server_too_big_response_becomes_error) {
    ServerProcess srv;
    Client c(srv.port());
    // Three 12 MiB keys: each SET fits, but KEYS would reply with ~36 MiB.
    for (char ch : std::string("abc")) {
        CHECK(c.cmd({"set", std::string(12u << 20, ch), "v"}).tag == TAG_NIL);
    }
    Value v = c.cmd({"keys"});
    CHECK(v.tag == TAG_ERR);
    CHECK_EQ(v.code, uint32_t(ERR_TOO_BIG));
    CHECK_EQ(c.text({"ping"}), std::string("(str) PONG\n"));  // connection still usable
}

TEST(server_idle_connections_are_closed) {
    ServerProcess srv({"--idle-timeout-ms", "300"});
    Client idle(srv.port());
    Client busy(srv.port());
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 6; i++) {  // 600 ms of activity, one request per 100 ms
        CHECK_EQ(busy.text({"ping"}), std::string("(str) PONG\n"));
        sleep_ms(100);
    }
    CHECK(idle.closed_by_peer());
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
    CHECK(ms >= 290);
    CHECK_EQ(busy.text({"ping"}), std::string("(str) PONG\n"));  // activity kept it alive
}

TEST(server_ttl_expires_in_background) {
    ServerProcess srv;
    Client c(srv.port());
    c.cmd({"set", "temp", "v"});
    c.cmd({"set", "keep", "v"});
    CHECK_EQ(c.cmd({"pexpire", "temp", "150"}).i, int64_t(1));
    int64_t ttl = c.cmd({"pttl", "temp"}).i;
    CHECK(ttl > 0 && ttl <= 150);
    sleep_ms(400);
    // DBSIZE does not look keys up, so only the timer can have removed it.
    CHECK_EQ(c.cmd({"dbsize"}).i, int64_t(1));
    CHECK_EQ(c.text({"get", "temp"}), std::string("(nil)\n"));
}

TEST(server_sorted_set_commands) {
    ServerProcess srv;
    Client c(srv.port());
    c.cmd({"zadd", "scores", "88", "carol"});
    c.cmd({"zadd", "scores", "95", "alice"});
    c.cmd({"zadd", "scores", "72", "bob"});
    CHECK_EQ(c.text({"zquery", "scores", "80", "", "0", "5"}),
             std::string("(arr) len=4\n(str) carol\n(dbl) 88\n(str) alice\n(dbl) 95\n(arr) end\n"));
    CHECK_EQ(c.cmd({"zrank", "scores", "alice"}).i, int64_t(2));
    CHECK_EQ(c.cmd({"zscore", "scores", "bob"}).d, 72.0);
    CHECK_EQ(c.cmd({"get", "scores"}).code, uint32_t(ERR_BAD_TYP));
}

TEST(server_deleting_huge_zset_keeps_serving) {
    ServerProcess srv;
    Client c(srv.port());
    std::string batch;
    for (int i = 0; i < 200000; i++) {
        batch += encode_request({"zadd", "huge", std::to_string(i), "member:" + std::to_string(i)});
    }
    c.send_raw(batch);
    for (int i = 0; i < 200000; i++) c.read_reply();
    CHECK_EQ(c.cmd({"zcard", "huge"}).i, int64_t(200000));
    auto t0 = std::chrono::steady_clock::now();
    CHECK_EQ(c.cmd({"del", "huge"}).i, int64_t(1));  // freed on a worker thread
    CHECK_EQ(c.text({"ping"}), std::string("(str) PONG\n"));
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
    std::printf("      (del of 200k-member zset + ping: %lld ms)\n", static_cast<long long>(ms));
    CHECK_EQ(srv.terminate(), 0);
}

TEST(server_many_clients_interleaved) {
    ServerProcess srv;
    std::vector<std::unique_ptr<Client>> clients;
    for (int i = 0; i < 64; i++) clients.push_back(std::make_unique<Client>(srv.port()));
    for (int round = 0; round < 20; round++) {
        for (size_t i = 0; i < clients.size(); i++) {
            clients[i]->send_raw(encode_request({"set", "c" + std::to_string(i), std::to_string(round)}));
        }
        for (auto &cl : clients) CHECK(cl->read_reply().tag == TAG_NIL);
    }
    CHECK_EQ(clients[0]->cmd({"keys"}).arr.size(), size_t(64));
    CHECK_EQ(clients[63]->text({"get", "c5"}), std::string("(str) 19\n"));
}

TEST(server_client_cli_end_to_end) {
    ServerProcess srv;
    const char *cli = std::getenv("CLIENT_BIN");
    if (!cli) throw testing::Failure{"CLIENT_BIN is not set"};
    std::string base = std::string(cli) + " --port " + std::to_string(srv.port()) + " ";
    auto run = [](const std::string &cmd) {
        std::string out;
        FILE *p = popen(cmd.c_str(), "r");
        char buf[256];
        while (size_t n = fread(buf, 1, sizeof buf, p)) out.append(buf, n);
        int status = pclose(p);
        return std::make_pair(out, WEXITSTATUS(status));
    };
    CHECK_EQ(run(base + "set name 'Ada Lovelace'").first, std::string("(nil)\n"));
    auto [out, code] = run(base + "get name");
    CHECK_EQ(out, std::string("(str) Ada Lovelace\n"));
    CHECK_EQ(code, 0);
    CHECK_EQ(run("printf 'zadd z 1 a\\nzadd z 2 b\\nzquery z 0 x 0 9\\n' | " + base).first,
             std::string("(int) 1\n(int) 1\n(arr) len=4\n(str) a\n(dbl) 1\n(str) b\n(dbl) 2\n(arr) end\n"));
}
