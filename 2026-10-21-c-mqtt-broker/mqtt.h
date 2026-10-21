#ifndef MQTT_H
#define MQTT_H

#include <stddef.h>
#include <stdint.h>

enum {
    MQTT_CONNECT = 1, MQTT_CONNACK = 2, MQTT_PUBLISH = 3, MQTT_PUBACK = 4,
    MQTT_SUBSCRIBE = 8, MQTT_SUBACK = 9, MQTT_UNSUBSCRIBE = 10, MQTT_UNSUBACK = 11,
    MQTT_PINGREQ = 12, MQTT_PINGRESP = 13, MQTT_DISCONNECT = 14
};

#define MQTT_MAX_FILTERS 16

struct mqtt_frame {
    uint8_t type, flags;
    const uint8_t *body;
    size_t body_len;
};

struct mqtt_connect {
    uint8_t level, flags;
    uint16_t keepalive;
    char client_id[256];
};

struct mqtt_publish {
    const uint8_t *topic;
    size_t topic_len;
    uint8_t qos, retain;
    uint16_t packet_id;
    const uint8_t *payload;
    size_t payload_len;
};

struct mqtt_subscribe {
    uint16_t packet_id;
    int count;
    struct { const uint8_t *filter; size_t len; uint8_t qos; } items[MQTT_MAX_FILTERS];
};

/* Remaining-length varint. decode: bytes used, 0 if incomplete, -1 if malformed. */
int mqtt_varint_encode(uint32_t v, uint8_t *out);
int mqtt_varint_decode(const uint8_t *buf, size_t len, uint32_t *v);

/* Split one packet off the front of buf: total bytes, 0 if incomplete, -1 if malformed. */
int mqtt_frame(const uint8_t *buf, size_t len, struct mqtt_frame *f);

int mqtt_parse_connect(const uint8_t *b, size_t len, struct mqtt_connect *c);
int mqtt_parse_publish(uint8_t flags, const uint8_t *b, size_t len, struct mqtt_publish *p);
/* with_qos: 1 for SUBSCRIBE (filter + qos byte), 0 for UNSUBSCRIBE (filter only). */
int mqtt_parse_subscribe(const uint8_t *b, size_t len, int with_qos, struct mqtt_subscribe *s);

/* Builders return the number of bytes written; out needs 5 + body_len bytes. */
size_t mqtt_build(uint8_t *out, uint8_t type, uint8_t flags, const uint8_t *body, size_t body_len);
size_t mqtt_build_publish(uint8_t *out, const uint8_t *topic, size_t tlen,
                          const uint8_t *payload, size_t plen);

/* Topic filters: '+' = one level, '#' = the rest (last level only). */
int mqtt_valid_filter(const char *f);
int mqtt_valid_topic(const char *t);
int mqtt_topic_match(const char *filter, const char *topic);

#endif
