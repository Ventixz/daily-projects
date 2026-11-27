// Command-line client: sends one command per argument group and prints the
// replies. Several commands can be sent in one go with `--` between them;
// they are written back to back (pipelined) and the replies read afterwards.
//
//   client set k v -- get k -- zadd z 1.5 alice
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "protocol.h"

static bool write_all(int fd, const uint8_t *p, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, p, n);
        if (rv < 0 && errno == EINTR) continue;
        if (rv <= 0) return false;
        p += rv;
        n -= size_t(rv);
    }
    return true;
}

int main(int argc, char **argv) {
    std::string host = "127.0.0.1";
    int port = 1234;
    int i = 1;
    for (; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--host") == 0) host = argv[i + 1];
        else if (strcmp(argv[i], "--port") == 0) port = atoi(argv[i + 1]);
        else break;
    }
    std::vector<std::vector<std::string>> cmds(1);
    for (; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) cmds.emplace_back();
        else cmds.back().push_back(argv[i]);
    }
    if (cmds.size() == 1 && cmds[0].empty()) {
        fprintf(stderr, "usage: %s [--host H] [--port P] cmd [args...] [-- cmd ...]\n", argv[0]);
        return 2;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(uint16_t(port));
    if (fd < 0 || inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1 ||
        connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0) {
        fprintf(stderr, "connect %s:%d: %s\n", host.c_str(), port, strerror(errno));
        return 1;
    }

    Buffer out;
    for (const auto &c : cmds) encode_request(out, c);
    if (!write_all(fd, out.data(), out.size())) {
        perror("write");
        return 1;
    }

    Buffer in;
    size_t pending = cmds.size();
    uint8_t buf[64 * 1024];
    while (pending > 0) {
        Value v;
        size_t used = 0;
        Parse r = parse_response(in.data(), in.size(), v, used);
        if (r == Parse::Error) {
            fprintf(stderr, "bad response\n");
            return 1;
        }
        if (r == Parse::Ok) {
            fputs(format_value(v).c_str(), stdout);
            in.erase(in.begin(), in.begin() + used);
            pending--;
            continue;
        }
        ssize_t rv = read(fd, buf, sizeof buf);
        if (rv <= 0) {
            fprintf(stderr, rv == 0 ? "connection closed by server\n" : "read error\n");
            return 1;
        }
        in.insert(in.end(), buf, buf + rv);
    }
    close(fd);
    return 0;
}
