#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kv.h"

static void print_stats(kv_store_t *db) {
    printf("memtable: %zu live entr%s\n", db->mt.len, db->mt.len == 1 ? "y" : "ies");
    printf("sstables (%zu, oldest first):\n", db->sstable_count);
    for (size_t i = 0; i < db->sstable_count; i++)
        printf("  %06u.sst  %zu record%s\n", db->sstables[i].id, db->sstables[i].count,
               db->sstables[i].count == 1 ? "" : "s");
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "kvdata";

    kv_store_t db;
    if (kv_open(&db, dir) != 0) {
        fprintf(stderr, "kv_open(%s) failed\n", dir);
        return 1;
    }
    printf("kv> opened '%s' (memtable flushes at %d entries, compacts at %d sstables)\n", dir,
           KV_MEMTABLE_FLUSH_THRESHOLD, KV_COMPACT_THRESHOLD);

    char line[4096];
    while (1) {
        printf("kv> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin))
            break;

        line[strcspn(line, "\n")] = '\0';
        char *cmd = strtok(line, " ");
        if (!cmd)
            continue;

        if (strcmp(cmd, "put") == 0) {
            char *key = strtok(NULL, " ");
            char *val = strtok(NULL, "");
            if (!key || !val) {
                printf("usage: put <key> <value...>\n");
                continue;
            }
            if (kv_put(&db, key, val, strlen(val)) != 0)
                printf("error\n");
            else
                printf("ok\n");
        } else if (strcmp(cmd, "get") == 0) {
            char *key = strtok(NULL, " ");
            if (!key) {
                printf("usage: get <key>\n");
                continue;
            }
            char *val;
            size_t vlen;
            int rc = kv_get(&db, key, &val, &vlen);
            if (rc < 0)
                printf("error\n");
            else if (rc == 0)
                printf("(not found)\n");
            else {
                printf("%.*s\n", (int)vlen, val);
                free(val);
            }
        } else if (strcmp(cmd, "del") == 0) {
            char *key = strtok(NULL, " ");
            if (!key) {
                printf("usage: del <key>\n");
                continue;
            }
            if (kv_del(&db, key) != 0)
                printf("error\n");
            else
                printf("ok\n");
        } else if (strcmp(cmd, "flush") == 0) {
            printf("%s\n", kv_flush(&db) == 0 ? "ok" : "error");
        } else if (strcmp(cmd, "compact") == 0) {
            printf("%s\n", kv_compact(&db) == 0 ? "ok" : "error");
        } else if (strcmp(cmd, "stats") == 0) {
            print_stats(&db);
        } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            break;
        } else {
            printf("commands: put <key> <value...> | get <key> | del <key> | flush | compact | stats | quit\n");
        }
    }

    kv_close(&db);
    return 0;
}
