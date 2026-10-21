/* A small MQTT 3.1.1 broker: QoS 0 delivery, QoS 1 publish acks, wildcards, keepalive. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "mqtt.h"

#define MAX_CLIENTS 128
#define MAX_SUBS 32
#define MAX_PACKET 65536

struct client {
    int fd;                 /* -1 = free slot */
    int connected, closing;
    time_t last_rx;
    unsigned keepalive;
    uint8_t *in, *out;
    size_t inlen, outlen;
    char id[256];
    char subs[MAX_SUBS][128];
    int nsubs;
};

static struct client clients[MAX_CLIENTS];

static void client_close(struct client *c)
{
    if (c->fd < 0) return;
    close(c->fd);
    free(c->in);
    free(c->out);
    memset(c, 0, sizeof *c);
    c->fd = -1;
}

static void queue(struct client *c, const uint8_t *data, size_t n)
{
    uint8_t *p = realloc(c->out, c->outlen + n);
    if (!p) { client_close(c); return; }
    c->out = p;
    memcpy(c->out + c->outlen, data, n);
    c->outlen += n;
}

static void flush(struct client *c)
{
    while (c->outlen) {
        ssize_t n = send(c->fd, c->out, c->outlen, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) return;
            client_close(c);
            return;
        }
        memmove(c->out, c->out + n, c->outlen - n);
        c->outlen -= n;
    }
    if (c->closing) client_close(c);
}

static int has_match(const struct client *c, const char *topic)
{
    for (int i = 0; i < c->nsubs; i++)
        if (mqtt_topic_match(c->subs[i], topic)) return 1;
    return 0;
}

static void publish(const struct mqtt_publish *p)
{
    char topic[1024];
    if (p->topic_len >= sizeof topic) return;
    memcpy(topic, p->topic, p->topic_len);
    topic[p->topic_len] = 0;
    if (!mqtt_valid_topic(topic)) return;

    uint8_t *pkt = malloc(8 + p->topic_len + p->payload_len);
    if (!pkt) return;
    size_t n = mqtt_build_publish(pkt, p->topic, p->topic_len, p->payload, p->payload_len);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        struct client *c = &clients[i];
        if (c->fd >= 0 && c->connected && has_match(c, topic)) queue(c, pkt, n); /* once per client */
    }
    free(pkt);
}

static void add_sub(struct client *c, const char *filter)
{
    for (int i = 0; i < c->nsubs; i++)
        if (!strcmp(c->subs[i], filter)) return;
    strcpy(c->subs[c->nsubs++], filter);
}

static void remove_sub(struct client *c, const char *filter)
{
    for (int i = 0; i < c->nsubs; i++)
        if (!strcmp(c->subs[i], filter)) {
            memmove(c->subs[i], c->subs[i + 1], (size_t)(c->nsubs - i - 1) * sizeof c->subs[0]);
            c->nsubs--;
            return;
        }
}

static void handle_connect(struct client *c, const struct mqtt_frame *f)
{
    struct mqtt_connect cn;
    uint8_t ack[2] = {0, 0}, pkt[8];
    if (c->connected || mqtt_parse_connect(f->body, f->body_len, &cn) < 0) { client_close(c); return; }
    if (cn.level != 4) {
        ack[1] = 1; /* unacceptable protocol version */
        queue(c, pkt, mqtt_build(pkt, MQTT_CONNACK, 0, ack, 2));
        c->closing = 1;
        return;
    }
    if (*cn.client_id)
        for (int i = 0; i < MAX_CLIENTS; i++) /* same id: the old session is taken over */
            if (&clients[i] != c && clients[i].fd >= 0 && !strcmp(clients[i].id, cn.client_id))
                client_close(&clients[i]);
    strcpy(c->id, cn.client_id);
    c->keepalive = cn.keepalive;
    c->connected = 1;
    queue(c, pkt, mqtt_build(pkt, MQTT_CONNACK, 0, ack, 2));
}

static void handle_subscribe(struct client *c, const struct mqtt_frame *f)
{
    struct mqtt_subscribe s;
    uint8_t body[2 + MQTT_MAX_FILTERS], pkt[2 + 5 + 2 + MQTT_MAX_FILTERS];
    int sub = f->type == MQTT_SUBSCRIBE;
    if (f->flags != 2 || mqtt_parse_subscribe(f->body, f->body_len, sub, &s) < 0) { client_close(c); return; }
    body[0] = (uint8_t)(s.packet_id >> 8);
    body[1] = (uint8_t)s.packet_id;
    for (int i = 0; i < s.count; i++) {
        char filter[128];
        int ok = s.items[i].len < sizeof filter;
        if (ok) {
            memcpy(filter, s.items[i].filter, s.items[i].len);
            filter[s.items[i].len] = 0;
            ok = mqtt_valid_filter(filter);
        }
        if (sub) {
            ok = ok && (c->nsubs < MAX_SUBS || has_match(c, filter));
            if (ok) add_sub(c, filter);
            body[2 + i] = ok ? 0 : 0x80; /* granted QoS 0 (we only deliver at QoS 0) */
        } else if (ok) {
            remove_sub(c, filter);
        }
    }
    queue(c, pkt, mqtt_build(pkt, sub ? MQTT_SUBACK : MQTT_UNSUBACK, 0, body, sub ? 2 + (size_t)s.count : 2));
}

static void handle_packet(struct client *c, const struct mqtt_frame *f)
{
    uint8_t pkt[8];
    if (!c->connected && f->type != MQTT_CONNECT) { client_close(c); return; }
    switch (f->type) {
    case MQTT_CONNECT:
        handle_connect(c, f);
        break;
    case MQTT_PUBLISH: {
        struct mqtt_publish p;
        if (mqtt_parse_publish(f->flags, f->body, f->body_len, &p) < 0 || p.qos == 2) { client_close(c); return; }
        if (p.qos == 1) {
            uint8_t id[2] = {(uint8_t)(p.packet_id >> 8), (uint8_t)p.packet_id};
            queue(c, pkt, mqtt_build(pkt, MQTT_PUBACK, 0, id, 2));
        }
        publish(&p);
        break;
    }
    case MQTT_SUBSCRIBE:
    case MQTT_UNSUBSCRIBE:
        handle_subscribe(c, f);
        break;
    case MQTT_PINGREQ:
        queue(c, pkt, mqtt_build(pkt, MQTT_PINGRESP, 0, NULL, 0));
        break;
    case MQTT_DISCONNECT:
        client_close(c);
        break;
    default:
        client_close(c);
    }
}

static void read_client(struct client *c)
{
    if (!c->in && !(c->in = malloc(MAX_PACKET))) { client_close(c); return; }
    ssize_t n = recv(c->fd, c->in + c->inlen, MAX_PACKET - c->inlen, 0);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) { client_close(c); return; }
    if (n < 0) return;
    c->inlen += (size_t)n;
    c->last_rx = time(NULL);

    size_t off = 0;
    while (c->fd >= 0 && !c->closing && off < c->inlen) {
        struct mqtt_frame f;
        int used = mqtt_frame(c->in + off, c->inlen - off, &f);
        if (used < 0 || (used == 0 && c->inlen - off == MAX_PACKET)) { client_close(c); return; }
        if (used == 0) break;
        handle_packet(c, &f);
        off += (size_t)used;
    }
    if (c->fd < 0) return;
    memmove(c->in, c->in + off, c->inlen - off);
    c->inlen -= off;
}

int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 1883;
    signal(SIGPIPE, SIG_IGN);
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].fd = -1;

    int lfd = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port),
                               .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t alen = sizeof addr;
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(lfd, 16) < 0 ||
        getsockname(lfd, (struct sockaddr *)&addr, &alen) < 0) {
        perror("bind/listen");
        return 1;
    }
    printf("listening on port %d\n", ntohs(addr.sin_port));
    fflush(stdout);

    for (;;) {
        struct pollfd fds[MAX_CLIENTS + 1];
        int idx[MAX_CLIENTS], nf = 1;
        fds[0] = (struct pollfd){.fd = lfd, .events = POLLIN};
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd < 0) continue;
            fds[nf] = (struct pollfd){.fd = clients[i].fd,
                                      .events = (clients[i].closing ? 0 : POLLIN) | (clients[i].outlen ? POLLOUT : 0)};
            idx[nf++] = i;
        }
        if (poll(fds, (nfds_t)nf, 1000) < 0 && errno != EINTR) break;

        if (fds[0].revents & POLLIN) {
            int fd = accept(lfd, NULL, NULL);
            if (fd >= 0) {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS && slot < 0; i++) if (clients[i].fd < 0) slot = i;
                if (slot < 0) { close(fd); }
                else {
                    fcntl(fd, F_SETFL, O_NONBLOCK);
                    memset(&clients[slot], 0, sizeof clients[slot]);
                    clients[slot].fd = fd;
                    clients[slot].last_rx = time(NULL);
                }
            }
        }
        for (int k = 1; k < nf; k++) {
            struct client *c = &clients[idx[k]];
            if (c->fd != fds[k].fd) continue; /* closed (and maybe reused) while handling another client */
            if (fds[k].revents & (POLLIN | POLLHUP | POLLERR) && !c->closing) read_client(c);
            if (c->fd >= 0 && c->outlen) flush(c);
        }
        time_t now = time(NULL);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            struct client *c = &clients[i];
            if (c->fd < 0) continue;
            if (c->outlen) flush(c); /* data queued for clients that were not polled this round */
            /* Keepalive: 1.5x the interval; unconnected sockets get 10s to send CONNECT. */
            if (c->fd >= 0 && now - c->last_rx > (c->connected ? (c->keepalive ? c->keepalive * 3 / 2 : 1 << 30) : 10))
                client_close(c);
        }
    }
    return 0;
}
