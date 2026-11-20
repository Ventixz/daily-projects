/* Shared helpers: limits, container_of, little-endian integers, monotonic clock. */
#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* A request or response body may not exceed this many bytes. */
#define K_MAX_MSG (32u << 20)
/* A request may not carry more strings than this. */
#define K_MAX_ARGS (200u * 1000u)

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* The wire format is little-endian regardless of the host. */
static inline uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline void wr_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static inline uint64_t rd_u64(const uint8_t *p) {
    return (uint64_t)rd_u32(p) | (uint64_t)rd_u32(p + 4) << 32;
}

static inline void wr_u64(uint8_t *p, uint64_t v) {
    wr_u32(p, (uint32_t)v);
    wr_u32(p + 4, (uint32_t)(v >> 32));
}

/* Milliseconds from CLOCK_MONOTONIC: immune to wall-clock jumps, so TTLs and
 * idle timers can't be broken by NTP or a user changing the time. */
static inline uint64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* FNV-1a. Good enough for a hashtable that never faces adversarial keys. */
static inline uint64_t str_hash(const uint8_t *data, size_t len) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < len; i++) h = (h + data[i]) * 0x01000193u;
    return h;
}

#endif
