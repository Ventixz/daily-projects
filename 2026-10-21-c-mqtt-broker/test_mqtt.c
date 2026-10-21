#include <stdio.h>
#include <string.h>

#include "mqtt.h"

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void test_varint(void)
{
    /* Boundary values from the MQTT spec table. */
    uint32_t vals[] = {0, 127, 128, 16383, 16384, 2097151, 2097152, 268435455};
    int sizes[] = {1, 1, 2, 2, 3, 3, 4, 4};
    for (int i = 0; i < 8; i++) {
        uint8_t b[4];
        uint32_t out = 99;
        int n = mqtt_varint_encode(vals[i], b);
        CHECK(n == sizes[i]);
        CHECK(mqtt_varint_decode(b, (size_t)n, &out) == n && out == vals[i]);
        if (n > 1) CHECK(mqtt_varint_decode(b, (size_t)n - 1, &out) == 0); /* incomplete */
    }
    uint8_t bad[5] = {0x80, 0x80, 0x80, 0x80, 0x01};
    uint32_t v;
    CHECK(mqtt_varint_decode(bad, 5, &v) == -1);
}

static void test_topics(void)
{
    CHECK(mqtt_topic_match("a/b/c", "a/b/c"));
    CHECK(!mqtt_topic_match("a/b/c", "a/b"));
    CHECK(!mqtt_topic_match("a/b", "a/b/c"));
    CHECK(!mqtt_topic_match("a/bc", "a/b"));
    CHECK(mqtt_topic_match("a/+/c", "a/x/c"));
    CHECK(!mqtt_topic_match("a/+/c", "a/x/y/c"));
    CHECK(mqtt_topic_match("a/+", "a/"));          /* empty level matches + */
    CHECK(!mqtt_topic_match("a/+", "a"));
    CHECK(mqtt_topic_match("+/+", "/finance"));
    CHECK(mqtt_topic_match("#", "anything/at/all"));
    CHECK(mqtt_topic_match("a/#", "a/b/c"));
    CHECK(mqtt_topic_match("a/#", "a"));           /* parent level matches too */
    CHECK(!mqtt_topic_match("a/#", "b/c"));
    CHECK(!mqtt_topic_match("#", "$SYS/uptime"));  /* $-topics skip root wildcards */
    CHECK(!mqtt_topic_match("+/uptime", "$SYS/uptime"));
    CHECK(mqtt_topic_match("$SYS/#", "$SYS/uptime"));

    CHECK(mqtt_valid_filter("a/+/b") && mqtt_valid_filter("#") && mqtt_valid_filter("a/#"));
    CHECK(!mqtt_valid_filter("") && !mqtt_valid_filter("a#") && !mqtt_valid_filter("a/#/b"));
    CHECK(!mqtt_valid_filter("a+/b") && !mqtt_valid_filter("a/b+"));
    CHECK(mqtt_valid_topic("a/b") && !mqtt_valid_topic("a/+") && !mqtt_valid_topic(""));
}

static void test_packets(void)
{
    uint8_t buf[512];
    struct mqtt_frame f;

    /* PUBLISH round trip, split mid-packet to check "incomplete" handling. */
    size_t n = mqtt_build_publish(buf, (const uint8_t *)"t/x", 3, (const uint8_t *)"hello", 5);
    CHECK(n == 2 + 2 + 3 + 5);
    CHECK(mqtt_frame(buf, 1, &f) == 0 && mqtt_frame(buf, n - 1, &f) == 0);
    CHECK(mqtt_frame(buf, n, &f) == (int)n && f.type == MQTT_PUBLISH);
    struct mqtt_publish p;
    CHECK(mqtt_parse_publish(f.flags, f.body, f.body_len, &p) == 0);
    CHECK(p.topic_len == 3 && !memcmp(p.topic, "t/x", 3) && p.payload_len == 5 && !memcmp(p.payload, "hello", 5));
    CHECK(mqtt_parse_publish(6, f.body, f.body_len, &p) == -1); /* QoS 3 */

    /* QoS 1 carries a packet id before the payload. */
    uint8_t q1[] = {0x32, 0x09, 0, 3, 'a', '/', 'b', 0x12, 0x34, 'h', 'i'};
    CHECK(mqtt_frame(q1, sizeof q1, &f) == (int)sizeof q1);
    CHECK(mqtt_parse_publish(f.flags, f.body, f.body_len, &p) == 0 && p.qos == 1 && p.packet_id == 0x1234);
    CHECK(p.payload_len == 2 && !memcmp(p.payload, "hi", 2));

    /* CONNECT */
    uint8_t con[] = {0x10, 0x11, 0, 4, 'M', 'Q', 'T', 'T', 4, 2, 0, 60, 0, 5, 'c', 'l', 'i', 'e', 'n'};
    con[1] = sizeof con - 2;
    struct mqtt_connect c;
    CHECK(mqtt_frame(con, sizeof con, &f) == (int)sizeof con);
    CHECK(mqtt_parse_connect(f.body, f.body_len, &c) == 0);
    CHECK(c.level == 4 && c.keepalive == 60 && !strcmp(c.client_id, "clien"));
    con[5] = 'X'; /* wrong protocol name */
    CHECK(mqtt_parse_connect(f.body, f.body_len, &c) == -1);

    /* SUBSCRIBE with two filters; UNSUBSCRIBE has no qos bytes. */
    uint8_t sub[] = {0x82, 0x0e, 0, 7, 0, 3, 'a', '/', '+', 0, 0, 3, 'b', '/', '#', 1};
    struct mqtt_subscribe s;
    CHECK(mqtt_frame(sub, sizeof sub, &f) == (int)sizeof sub && f.flags == 2);
    CHECK(mqtt_parse_subscribe(f.body, f.body_len, 1, &s) == 0);
    CHECK(s.packet_id == 7 && s.count == 2 && s.items[0].len == 3 && s.items[1].qos == 1);
    uint8_t unsub[] = {0xa2, 0x07, 0, 9, 0, 3, 'a', '/', '+'};
    CHECK(mqtt_frame(unsub, sizeof unsub, &f) == (int)sizeof unsub);
    CHECK(mqtt_parse_subscribe(f.body, f.body_len, 0, &s) == 0 && s.count == 1);
    uint8_t trunc[] = {0x82, 0x06, 0, 7, 0, 5, 'a', '/'}; /* filter longer than the packet */
    CHECK(mqtt_frame(trunc, sizeof trunc, &f) == (int)sizeof trunc);
    CHECK(mqtt_parse_subscribe(f.body, f.body_len, 1, &s) == -1);
}

int main(void)
{
    test_varint();
    test_topics();
    test_packets();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
