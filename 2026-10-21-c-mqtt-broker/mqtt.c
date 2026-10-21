#include "mqtt.h"

#include <string.h>

int mqtt_varint_encode(uint32_t v, uint8_t *out)
{
    int n = 0;
    do {
        uint8_t d = v % 128;
        v /= 128;
        if (v) d |= 128;
        out[n++] = d;
    } while (v);
    return n;
}

int mqtt_varint_decode(const uint8_t *b, size_t len, uint32_t *v)
{
    uint32_t val = 0, mult = 1;
    for (int i = 0; i < 4; i++) {
        if ((size_t)i >= len) return 0;
        val += (b[i] & 127) * mult;
        if (!(b[i] & 128)) { *v = val; return i + 1; }
        mult *= 128;
    }
    return -1;
}

int mqtt_frame(const uint8_t *buf, size_t len, struct mqtt_frame *f)
{
    uint32_t rem;
    if (len < 2) return 0;
    int n = mqtt_varint_decode(buf + 1, len - 1, &rem);
    if (n <= 0) return n;
    size_t total = 1 + (size_t)n + rem;
    if (len < total) return 0;
    f->type = buf[0] >> 4;
    f->flags = buf[0] & 15;
    f->body = buf + 1 + n;
    f->body_len = rem;
    return (int)total;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* Length-prefixed string at *off; advances *off. */
static int read_str(const uint8_t *b, size_t len, size_t *off, const uint8_t **s, size_t *sl)
{
    if (*off + 2 > len) return -1;
    size_t n = be16(b + *off);
    if (*off + 2 + n > len) return -1;
    *s = b + *off + 2;
    *sl = n;
    *off += 2 + n;
    return 0;
}

int mqtt_parse_connect(const uint8_t *b, size_t len, struct mqtt_connect *c)
{
    size_t off = 0, sl;
    const uint8_t *s;
    if (read_str(b, len, &off, &s, &sl) < 0) return -1;
    /* Accept the v3.1 name too, so the broker can answer with "unacceptable protocol version". */
    if (!(sl == 4 && !memcmp(s, "MQTT", 4)) && !(sl == 6 && !memcmp(s, "MQIsdp", 6))) return -1;
    if (off + 4 > len) return -1;
    c->level = b[off++];
    c->flags = b[off++];
    c->keepalive = be16(b + off);
    off += 2;
    if (c->flags & 1) return -1; /* reserved bit must be 0 */
    if (read_str(b, len, &off, &s, &sl) < 0 || sl >= sizeof c->client_id) return -1;
    memcpy(c->client_id, s, sl);
    c->client_id[sl] = 0;
    return 0; /* will / username / password payload is ignored */
}

int mqtt_parse_publish(uint8_t flags, const uint8_t *b, size_t len, struct mqtt_publish *p)
{
    size_t off = 0;
    p->qos = (flags >> 1) & 3;
    p->retain = flags & 1;
    p->packet_id = 0;
    if (p->qos == 3) return -1;
    if (read_str(b, len, &off, &p->topic, &p->topic_len) < 0) return -1;
    if (p->qos > 0) {
        if (off + 2 > len) return -1;
        p->packet_id = be16(b + off);
        off += 2;
    }
    p->payload = b + off;
    p->payload_len = len - off;
    return 0;
}

int mqtt_parse_subscribe(const uint8_t *b, size_t len, int with_qos, struct mqtt_subscribe *s)
{
    size_t off = 2;
    if (len < 2) return -1;
    s->packet_id = be16(b);
    s->count = 0;
    while (off < len) {
        if (s->count >= MQTT_MAX_FILTERS) return -1;
        if (read_str(b, len, &off, &s->items[s->count].filter, &s->items[s->count].len) < 0) return -1;
        s->items[s->count].qos = 0;
        if (with_qos) {
            if (off >= len) return -1;
            s->items[s->count].qos = b[off++];
        }
        s->count++;
    }
    return s->count > 0 ? 0 : -1;
}

size_t mqtt_build(uint8_t *out, uint8_t type, uint8_t flags, const uint8_t *body, size_t body_len)
{
    size_t n = 0;
    out[n++] = (uint8_t)(type << 4 | (flags & 15));
    n += mqtt_varint_encode((uint32_t)body_len, out + n);
    if (body_len) memcpy(out + n, body, body_len);
    return n + body_len;
}

size_t mqtt_build_publish(uint8_t *out, const uint8_t *topic, size_t tlen,
                          const uint8_t *payload, size_t plen)
{
    size_t n = 0;
    out[n++] = MQTT_PUBLISH << 4; /* QoS 0, no retain */
    n += mqtt_varint_encode((uint32_t)(2 + tlen + plen), out + n);
    out[n++] = (uint8_t)(tlen >> 8);
    out[n++] = (uint8_t)tlen;
    memcpy(out + n, topic, tlen);
    n += tlen;
    if (plen) memcpy(out + n, payload, plen);
    return n + plen;
}

int mqtt_valid_filter(const char *f)
{
    if (!*f) return 0;
    for (const char *p = f; *p; p++) {
        int level_start = p == f || p[-1] == '/';
        int level_end = !p[1] || p[1] == '/';
        if (*p == '+' && !(level_start && level_end)) return 0;
        if (*p == '#' && !(level_start && !p[1])) return 0;
    }
    return 1;
}

int mqtt_valid_topic(const char *t)
{
    return *t && !strpbrk(t, "+#");
}

int mqtt_topic_match(const char *f, const char *t)
{
    /* Wildcards in the first level never match $SYS-style topics. */
    if (*t == '$' && (*f == '+' || *f == '#')) return 0;
    for (;;) {
        if (*f == '#') return 1;
        if (*f == '+') {
            while (*t && *t != '/') t++;
            f++;
        } else {
            while (*f && *f != '/' && *t && *t != '/') {
                if (*f != *t) return 0;
                f++; t++;
            }
            if ((*f && *f != '/') || (*t && *t != '/')) return 0;
        }
        if (!*t) return !*f || strcmp(f, "/#") == 0; /* "a/#" also matches "a" */
        if (*f != '/') return 0;
        f++; t++;
    }
}
