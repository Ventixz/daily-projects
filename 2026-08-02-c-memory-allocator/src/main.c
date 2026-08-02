/* Interactive REPL over the allocator in allocator.c. Slots are just this
 * demo's bookkeeping (an id -> pointer table) so commands can refer back to
 * a previous allocation; the allocator itself has no notion of "slots". */
#include "allocator.h"

#include <stdio.h>
#include <string.h>

#define MAX_SLOTS 64

static void *slots[MAX_SLOTS];
static size_t sizes[MAX_SLOTS];

static int free_slot(void) {
    for (int i = 0; i < MAX_SLOTS; i++)
        if (!slots[i]) return i;
    return -1;
}

int main(void) {
    char line[256];
    printf("simple-alloc> commands: alloc <n>, calloc <n> <sz>, realloc <id> <n>, "
           "free <id>, write <id> <text>, read <id>, list, quit\n");

    while (1) {
        printf("alloc> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        char cmd[16] = {0};
        int a = 0, b = 0;
        char text[256] = {0};
        if (sscanf(line, "%15s", cmd) != 1) continue;

        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            break;
        } else if (strcmp(cmd, "list") == 0) {
            print_mem_list();
        } else if (strcmp(cmd, "alloc") == 0) {
            sscanf(line, "%*s %d", &a);
            int id = free_slot();
            if (id < 0) { printf("no free slots\n"); continue; }
            slots[id] = my_malloc((size_t)a);
            sizes[id] = (size_t)a;
            printf("id %d -> %p (%d bytes)\n", id, slots[id], a);
        } else if (strcmp(cmd, "calloc") == 0) {
            sscanf(line, "%*s %d %d", &a, &b);
            int id = free_slot();
            if (id < 0) { printf("no free slots\n"); continue; }
            slots[id] = my_calloc((size_t)a, (size_t)b);
            sizes[id] = (size_t)a * (size_t)b;
            printf("id %d -> %p (%d x %d bytes, zeroed)\n", id, slots[id], a, b);
        } else if (strcmp(cmd, "realloc") == 0) {
            sscanf(line, "%*s %d %d", &a, &b);
            if (a < 0 || a >= MAX_SLOTS) { printf("bad id\n"); continue; }
            slots[a] = my_realloc(slots[a], (size_t)b);
            sizes[a] = (size_t)b;
            printf("id %d -> %p (%d bytes)\n", a, slots[a], b);
        } else if (strcmp(cmd, "free") == 0) {
            sscanf(line, "%*s %d", &a);
            if (a < 0 || a >= MAX_SLOTS) { printf("bad id\n"); continue; }
            my_free(slots[a]);
            slots[a] = NULL;
            sizes[a] = 0;
            printf("freed %d\n", a);
        } else if (strcmp(cmd, "write") == 0) {
            sscanf(line, "%*s %d %255[^\n]", &a, text);
            if (a < 0 || a >= MAX_SLOTS || !slots[a]) { printf("bad id\n"); continue; }
            size_t len = strlen(text);
            if (sizes[a] > 0 && len >= sizes[a]) len = sizes[a] - 1;
            memcpy(slots[a], text, len);
            ((char *)slots[a])[len] = '\0';
            printf("wrote %zu bytes into %d\n", len, a);
        } else if (strcmp(cmd, "read") == 0) {
            sscanf(line, "%*s %d", &a);
            if (a < 0 || a >= MAX_SLOTS || !slots[a]) { printf("bad id\n"); continue; }
            printf("%s\n", (char *)slots[a]);
        } else {
            printf("unknown command: %s\n", cmd);
        }
    }

    return 0;
}
