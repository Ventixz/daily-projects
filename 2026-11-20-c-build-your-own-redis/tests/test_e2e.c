/* End-to-end tests: start the real server binary on a free port and talk to it
 * over TCP. Usage: test_e2e PATH_TO_SERVER */
#define _DEFAULT_SOURCE
#include "../src/buf.h"
#include "../src/common.h"
#include "../src/proto.h"
#include "test.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *g_server_path;

typedef struct {
    pid_t pid;
    int port;
} ServerProc;

static ServerProc start_server(const char *idle_ms) {
    int pipefd[2];
    if (pipe(pipefd) < 0) { perror("pipe"); exit(1); }
    pid_t pid = fork();
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execl(g_server_path, g_server_path, "-p", "0", "-i", idle_ms, (char *)NULL);
        perror("execl");
        _exit(127);
    }
    close(pipefd[1]);
    /* The server prints its port once it is listening. */
    char line[128] = {0};
    size_t n = 0;
    while (n < sizeof line - 1) {
        ssize_t rv = read(pipefd[0], line + n, 1);
        if (rv <= 0 || line[n] == '\n') break;
        n++;
    }
    close(pipefd[0]);
    ServerProc s = {.pid = pid, .port = 0};
    char *colon = strrchr(line, ':');
    if (colon) s.port = atoi(colon + 1);
    if (s.port <= 0) { fprintf(stderr, "server did not start: '%s'\n", line); exit(1); }
    return s;
}

/* SIGTERM triggers a clean shutdown; a sanitizer build exits non-zero if the
 * shutdown leaked memory, which this catches. */
static int stop_server(ServerProc s) {
    kill(s.pid, SIGTERM);
    int status = 0;
    waitpid(s.pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static int connect_to(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("connect"); exit(1); }
    return fd;
}

static void send_all(int fd, const void *p, size_t n) {
    const uint8_t *b = p;
    while (n) {
        ssize_t rv = write(fd, b, n);
        if (rv <= 0) { perror("write"); return; }
        b += rv;
        n -= (size_t)rv;
    }
}

/* Reads exactly n bytes, giving up after timeout_ms. Returns bytes read; fewer
 * than n means EOF or timeout. */
static size_t recv_n(int fd, uint8_t *p, size_t n, int timeout_ms) {
    size_t got = 0;
    while (got < n) {
        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        if (poll(&pfd, 1, timeout_ms) <= 0) break;
        ssize_t rv = read(fd, p + got, n - got);
        if (rv <= 0) break;
        got += (size_t)rv;
    }
    return got;
}

static void encode_line(Buf *b, const char *line) {
    char copy[512];
    snprintf(copy, sizeof copy, "%s", line);
    Str args[16];
    size_t n = 0;
    for (char *tok = strtok(copy, " "); tok && n < 16; tok = strtok(NULL, " ")) args[n++] = (Str){tok, strlen(tok)};
    encode_request(b, args, n);
}

/* Reads one framed reply and returns it printed (static buffer). */
static char g_reply[1 << 16];
static const char *read_reply(int fd) {
    uint8_t hdr[4];
    if (recv_n(fd, hdr, 4, 2000) != 4) return "<eof>";
    uint32_t len = rd_u32(hdr);
    uint8_t *body = malloc(len ? len : 1);
    if (recv_n(fd, body, len, 2000) != len) { free(body); return "<short>"; }
    FILE *f = fmemopen(g_reply, sizeof g_reply, "w");
    print_value(f, body, len);
    fclose(f);
    free(body);
    size_t l = strlen(g_reply);
    if (l && g_reply[l - 1] == '\n') g_reply[l - 1] = '\0';
    return g_reply;
}

static const char *call(int fd, const char *line) {
    Buf b = {0};
    encode_line(&b, line);
    send_all(fd, buf_data(&b), b.len);
    buf_free(&b);
    return read_reply(fd);
}

static void test_basic_commands_and_two_clients(void) {
    ServerProc s = start_server("5000");
    int a = connect_to(s.port), b = connect_to(s.port);
    CHECK(strcmp(call(a, "set k hello"), "(nil)") == 0);
    CHECK(strcmp(call(b, "get k"), "(str) hello") == 0);  /* shared keyspace */
    CHECK(strcmp(call(b, "zadd lb 10 ann"), "(int) 1") == 0);
    CHECK(strcmp(call(a, "zscore lb ann"), "(dbl) 10") == 0);
    CHECK(strcmp(call(a, "bogus"), "(err) 1 unknown command") == 0);
    close(a);
    close(b);
    CHECK(stop_server(s) == 0);
}

static void test_pipelining_and_fragmented_writes(void) {
    ServerProc s = start_server("5000");
    int fd = connect_to(s.port);
    /* Three requests in one write: three replies, in order. */
    Buf b = {0};
    encode_line(&b, "set p 1");
    encode_line(&b, "get p");
    encode_line(&b, "del p");
    send_all(fd, buf_data(&b), b.len);
    buf_free(&b);
    CHECK(strcmp(read_reply(fd), "(nil)") == 0);
    CHECK(strcmp(read_reply(fd), "(str) 1") == 0);
    CHECK(strcmp(read_reply(fd), "(int) 1") == 0);

    /* One request dribbled out a byte at a time must still parse. */
    encode_line(&b, "set frag ok");
    for (size_t i = 0; i < b.len; i++) {
        send_all(fd, buf_data(&b) + i, 1);
        usleep(1000);
    }
    buf_free(&b);
    CHECK(strcmp(read_reply(fd), "(nil)") == 0);
    CHECK(strcmp(call(fd, "get frag"), "(str) ok") == 0);
    close(fd);
    CHECK(stop_server(s) == 0);
}

static void test_large_value_round_trip(void) {
    ServerProc s = start_server("5000");
    int fd = connect_to(s.port);
    size_t big = 4u << 20;  /* 4 MiB: many partial reads and writes */
    char *val = malloc(big);
    for (size_t i = 0; i < big; i++) val[i] = (char)('a' + i % 26);
    Str set[3] = {{"set", 3}, {"big", 3}, {val, big}};
    Buf b = {0};
    encode_request(&b, set, 3);
    send_all(fd, buf_data(&b), b.len);
    buf_free(&b);
    CHECK(strcmp(read_reply(fd), "(nil)") == 0);

    Str get[2] = {{"get", 3}, {"big", 3}};
    encode_request(&b, get, 2);
    send_all(fd, buf_data(&b), b.len);
    buf_free(&b);
    uint8_t hdr[4];
    CHECK(recv_n(fd, hdr, 4, 2000) == 4);
    uint32_t len = rd_u32(hdr);
    CHECK(len == 1 + 4 + big);
    uint8_t *body = malloc(len);
    CHECK(recv_n(fd, body, len, 5000) == len);
    CHECK(body[0] == TAG_STR && rd_u32(body + 1) == big && memcmp(body + 5, val, big) == 0);
    free(body);
    free(val);
    close(fd);
    CHECK(stop_server(s) == 0);
}

static void test_bad_input_closes_only_that_connection(void) {
    ServerProc s = start_server("5000");
    int good = connect_to(s.port);
    CHECK(strcmp(call(good, "set survivor yes"), "(nil)") == 0);

    int bad = connect_to(s.port);
    uint8_t too_long[4];
    wr_u32(too_long, K_MAX_MSG + 1);
    send_all(bad, too_long, 4);
    uint8_t tmp[1];
    CHECK(recv_n(bad, tmp, 1, 2000) == 0);  /* server hung up */
    close(bad);

    int garbage = connect_to(s.port);
    uint8_t junk[] = {6, 0, 0, 0, 1, 0, 0, 0, 99, 0};  /* string longer than body */
    send_all(garbage, junk, sizeof junk);
    CHECK(recv_n(garbage, tmp, 1, 2000) == 0);
    close(garbage);

    CHECK(strcmp(call(good, "get survivor"), "(str) yes") == 0);
    close(good);
    CHECK(stop_server(s) == 0);
}

static void test_idle_timeout_and_active_expiry(void) {
    ServerProc s = start_server("300");
    int idle = connect_to(s.port);
    int busy = connect_to(s.port);
    CHECK(strcmp(call(busy, "set gone soon"), "(nil)") == 0);
    CHECK(strcmp(call(busy, "pexpire gone 100"), "(int) 1") == 0);
    CHECK(strcmp(call(busy, "set stays here"), "(nil)") == 0);
    /* Keep `busy` active while `idle` says nothing for longer than 300 ms. */
    for (int i = 0; i < 5; i++) {
        usleep(100 * 1000);
        CHECK(strcmp(call(busy, "pttl stays"), "(int) -1") == 0);
    }
    uint8_t tmp[1];
    CHECK(recv_n(idle, tmp, 1, 1000) == 0);  /* closed by the server */
    /* The TTL key was reaped by the timer, not by a read touching it. */
    CHECK(strcmp(call(busy, "keys"), "(arr) len=1\n(str) stays\n(arr) end") == 0);
    close(idle);
    close(busy);
    CHECK(stop_server(s) == 0);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s PATH_TO_SERVER\n", argv[0]);
        return 2;
    }
    g_server_path = argv[1];
    signal(SIGPIPE, SIG_IGN);
    RUN(test_basic_commands_and_two_clients);
    RUN(test_pipelining_and_fragmented_writes);
    RUN(test_large_value_round_trip);
    RUN(test_bad_input_closes_only_that_connection);
    RUN(test_idle_timeout_and_active_expiry);
    DONE();
}
