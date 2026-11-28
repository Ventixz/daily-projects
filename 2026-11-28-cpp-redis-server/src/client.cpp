// Command-line client.
//   client set greeting hello      one command, reply printed redis-cli style
//   client                         read commands from stdin, one per line
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "protocol.h"

namespace {

bool write_all(int fd, const char *p, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, p, n);
        if (rv < 0 && errno == EINTR) continue;
        if (rv <= 0) return false;
        p += rv;
        n -= size_t(rv);
    }
    return true;
}

bool read_full(int fd, char *p, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, p, n);
        if (rv < 0 && errno == EINTR) continue;
        if (rv <= 0) return false;
        p += rv;
        n -= size_t(rv);
    }
    return true;
}

bool round_trip(int fd, const std::vector<std::string> &args) {
    std::string req = encode_request(args);
    if (req.size() - 4 > k_max_msg) {
        std::fprintf(stderr, "client: request too large\n");
        return false;
    }
    if (!write_all(fd, req.data(), req.size())) {
        std::fprintf(stderr, "client: write failed\n");
        return false;
    }
    char hdr[4];
    if (!read_full(fd, hdr, 4)) {
        std::fprintf(stderr, "client: connection closed by server\n");
        return false;
    }
    const auto *p = reinterpret_cast<const uint8_t *>(hdr);
    uint32_t len = 0;
    get_u32(p, p + 4, len);
    if (len > k_max_msg) {
        std::fprintf(stderr, "client: reply too long\n");
        return false;
    }
    std::string body(len, '\0');
    if (!read_full(fd, body.data(), len)) {
        std::fprintf(stderr, "client: truncated reply\n");
        return false;
    }
    Value v;
    if (!decode_response(reinterpret_cast<const uint8_t *>(body.data()), body.size(), v)) {
        std::fprintf(stderr, "client: malformed reply\n");
        return false;
    }
    std::fputs(format_value(v).c_str(), stdout);
    std::fflush(stdout);
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    std::string host = "127.0.0.1";
    int port = 1234;
    int i = 1;
    for (; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--host") == 0) {
            host = argv[i + 1];
        } else if (std::strcmp(argv[i], "--port") == 0) {
            port = std::atoi(argv[i + 1]);
        } else {
            break;
        }
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (fd < 0 || inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1 ||
        connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0) {
        std::fprintf(stderr, "client: cannot connect to %s:%d: %s\n", host.c_str(), port,
                     std::strerror(errno));
        return 1;
    }

    if (i < argc) {
        std::vector<std::string> args(argv + i, argv + argc);
        bool ok = round_trip(fd, args);
        close(fd);
        return ok ? 0 : 1;
    }

    // No command on the command line: one command per stdin line, split on
    // whitespace (so values with spaces need the one-shot form above).
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream words(line);
        std::vector<std::string> args;
        for (std::string w; words >> w;) args.push_back(w);
        if (args.empty()) continue;
        if (!round_trip(fd, args)) {
            close(fd);
            return 1;
        }
    }
    close(fd);
    return 0;
}
