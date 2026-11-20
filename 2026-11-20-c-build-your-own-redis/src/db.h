/* The keyspace and the command table. Nothing here touches sockets or the
 * clock: the caller passes `now_ms`, which is what makes TTLs testable. */
#ifndef DB_H
#define DB_H

#include "buf.h"
#include "hashtable.h"
#include "heap.h"
#include "proto.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    HMap keys;
    Heap ttl;  /* min-heap of expiry times, one item per key that has a TTL */
} DB;

void db_init(DB *db);
void db_free(DB *db);
size_t db_size(DB *db);

/* Run one command, appending exactly one response value to `out`. */
void db_execute(DB *db, const Strs *cmd, Buf *out, uint64_t now_ms);
/* Delete keys whose TTL has passed, at most max_work of them. Returns how many
 * were deleted. The bound keeps one event-loop tick short. */
size_t db_expire(DB *db, uint64_t now_ms, size_t max_work);
/* Earliest expiry time, or UINT64_MAX if no key has a TTL. */
uint64_t db_next_expiry(const DB *db);

#endif
