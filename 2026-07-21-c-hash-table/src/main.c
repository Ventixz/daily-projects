#include <stdio.h>
#include <string.h>

#include "hash_table.h"

/* A tiny REPL over the hash table: `set key value`, `get key`,
 * `del key`, `quit`. Reads from stdin so it works both interactively
 * and piped (`echo "set a 1" | ./hash_table_demo`). */
int main(void) {
    ht_hash_table* ht = ht_new();
    char line[256];

    printf("hash table demo - commands: set <k> <v> | get <k> | del <k> | quit\n");
    while (fgets(line, sizeof(line), stdin)) {
        char cmd[16], key[128], value[128];
        int n = sscanf(line, "%15s %127s %127s", cmd, key, value);

        if (n >= 1 && strcmp(cmd, "quit") == 0) {
            break;
        } else if (n == 3 && strcmp(cmd, "set") == 0) {
            ht_insert(ht, key, value);
            printf("ok\n");
        } else if (n == 2 && strcmp(cmd, "get") == 0) {
            char* v = ht_search(ht, key);
            printf("%s\n", v ? v : "(nil)");
        } else if (n == 2 && strcmp(cmd, "del") == 0) {
            ht_delete(ht, key);
            printf("ok\n");
        } else {
            printf("usage: set <k> <v> | get <k> | del <k> | quit\n");
        }
    }

    ht_del_hash_table(ht);
    return 0;
}
