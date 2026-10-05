/*
 * A tiny SQLite-style database: REPL -> parser -> pager -> B-tree.
 * Follows "Let's Build a Simple Database" (cstack.github.io/db_tutorial).
 *
 * Tree shape: the root (page 0) is either a leaf or an internal node whose
 * children are all leaves (depth <= 2). Leaves are chained for full scans.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define USERNAME_MAX 32
#define EMAIL_MAX 255
#define PAGE_SIZE 4096
#define TABLE_MAX_PAGES 400

typedef struct {
    uint32_t id;
    char username[USERNAME_MAX + 1];
    char email[EMAIL_MAX + 1];
} Row;

#define ID_SIZE 4
#define ROW_SIZE (ID_SIZE + USERNAME_MAX + 1 + EMAIL_MAX + 1)

/* Common node header: type(1) is_root(1) count(4). */
enum { NODE_INTERNAL = 0, NODE_LEAF = 1 };
#define OFF_TYPE 0
#define OFF_ROOT 1
#define OFF_COUNT 2
#define COMMON_HDR 6

/* Leaf: header + next_leaf(4), then cells of key(4)+row. */
#define OFF_NEXT 6
#define LEAF_HDR 10
#define CELL_SIZE (4 + ROW_SIZE)
#define LEAF_MAX_CELLS ((PAGE_SIZE - LEAF_HDR) / CELL_SIZE)
#define LEAF_RIGHT_SPLIT ((LEAF_MAX_CELLS + 1) / 2)
#define LEAF_LEFT_SPLIT ((LEAF_MAX_CELLS + 1) - LEAF_RIGHT_SPLIT)

/* Internal: header + right_child(4), then (child(4), key(4)) cells. */
#define OFF_RIGHT 6
#define INTERNAL_HDR 10
#define INTERNAL_CELL 8
#define INTERNAL_MAX_KEYS ((PAGE_SIZE - INTERNAL_HDR) / INTERNAL_CELL)

typedef struct {
    int fd;
    uint32_t file_length;
    uint32_t num_pages;
    uint8_t *pages[TABLE_MAX_PAGES];
} Pager;

typedef struct {
    Pager *pager;
} Table;

typedef struct {
    Table *table;
    uint32_t page, cell;
    int end;
} Cursor;

/* ---- raw field access ---- */
static uint32_t rd(uint8_t *p, size_t off) { uint32_t v; memcpy(&v, p + off, 4); return v; }
static void wr(uint8_t *p, size_t off, uint32_t v) { memcpy(p + off, &v, 4); }

static int is_leaf(uint8_t *n) { return n[OFF_TYPE] == NODE_LEAF; }
static uint32_t count(uint8_t *n) { return rd(n, OFF_COUNT); }
static void set_count(uint8_t *n, uint32_t c) { wr(n, OFF_COUNT, c); }

static uint8_t *leaf_cell(uint8_t *n, uint32_t i) { return n + LEAF_HDR + i * CELL_SIZE; }
static uint32_t leaf_key(uint8_t *n, uint32_t i) { return rd(leaf_cell(n, i), 0); }

static uint32_t int_child(uint8_t *n, uint32_t i) {
    return i == count(n) ? rd(n, OFF_RIGHT) : rd(n, INTERNAL_HDR + i * INTERNAL_CELL);
}
static void set_int_child(uint8_t *n, uint32_t i, uint32_t page) {
    if (i == count(n)) wr(n, OFF_RIGHT, page);
    else wr(n, INTERNAL_HDR + i * INTERNAL_CELL, page);
}
static uint32_t int_key(uint8_t *n, uint32_t i) { return rd(n, INTERNAL_HDR + i * INTERNAL_CELL + 4); }
static void set_int_key(uint8_t *n, uint32_t i, uint32_t k) { wr(n, INTERNAL_HDR + i * INTERNAL_CELL + 4, k); }

/* ---- serialization ---- */
static void serialize_row(const Row *r, uint8_t *dst) {
    memcpy(dst, &r->id, ID_SIZE);
    memcpy(dst + ID_SIZE, r->username, USERNAME_MAX + 1);
    memcpy(dst + ID_SIZE + USERNAME_MAX + 1, r->email, EMAIL_MAX + 1);
}
static void deserialize_row(const uint8_t *src, Row *r) {
    memcpy(&r->id, src, ID_SIZE);
    memcpy(r->username, src + ID_SIZE, USERNAME_MAX + 1);
    memcpy(r->email, src + ID_SIZE + USERNAME_MAX + 1, EMAIL_MAX + 1);
}

/* ---- pager ---- */
static Pager *pager_open(const char *filename) {
    int fd = open(filename, O_RDWR | O_CREAT, 0600);
    if (fd < 0) { perror("open"); exit(1); }
    off_t len = lseek(fd, 0, SEEK_END);
    if (len % PAGE_SIZE != 0) { fprintf(stderr, "Corrupt db file: not a whole number of pages.\n"); exit(1); }
    Pager *p = calloc(1, sizeof *p);
    p->fd = fd;
    p->file_length = (uint32_t)len;
    p->num_pages = (uint32_t)(len / PAGE_SIZE);
    return p;
}

static uint8_t *get_page(Pager *p, uint32_t n) {
    if (n >= TABLE_MAX_PAGES) { fprintf(stderr, "Page %u out of bounds.\n", n); exit(1); }
    if (!p->pages[n]) {
        uint8_t *page = calloc(1, PAGE_SIZE);
        uint32_t on_disk = p->file_length / PAGE_SIZE;
        if (n < on_disk) {
            lseek(p->fd, (off_t)n * PAGE_SIZE, SEEK_SET);
            if (read(p->fd, page, PAGE_SIZE) != PAGE_SIZE) { perror("read"); exit(1); }
        }
        p->pages[n] = page;
        if (n >= p->num_pages) p->num_pages = n + 1;
    }
    return p->pages[n];
}

static void pager_flush(Pager *p, uint32_t n) {
    if (!p->pages[n]) return;
    lseek(p->fd, (off_t)n * PAGE_SIZE, SEEK_SET);
    if (write(p->fd, p->pages[n], PAGE_SIZE) != PAGE_SIZE) { perror("write"); exit(1); }
}

static uint32_t new_page(Pager *p) { return p->num_pages; } /* get_page extends */

/* ---- table ---- */
static void init_leaf(uint8_t *n) {
    memset(n, 0, PAGE_SIZE);
    n[OFF_TYPE] = NODE_LEAF;
}

static Table *db_open(const char *filename) {
    Pager *p = pager_open(filename);
    Table *t = calloc(1, sizeof *t);
    t->pager = p;
    if (p->num_pages == 0) {
        uint8_t *root = get_page(p, 0);
        init_leaf(root);
        root[OFF_ROOT] = 1;
    }
    return t;
}

static void db_close(Table *t) {
    Pager *p = t->pager;
    for (uint32_t i = 0; i < p->num_pages; i++) {
        pager_flush(p, i);
        free(p->pages[i]);
    }
    close(p->fd);
    free(p);
    free(t);
}

/* ---- cursors ---- */
static uint32_t leaf_lower_bound(uint8_t *leaf, uint32_t key) {
    uint32_t lo = 0, hi = count(leaf);
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (leaf_key(leaf, mid) < key) lo = mid + 1; else hi = mid;
    }
    return lo;
}

static uint32_t internal_child_index(uint8_t *n, uint32_t key) {
    uint32_t lo = 0, hi = count(n); /* index == count means right child */
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (int_key(n, mid) >= key) hi = mid; else lo = mid + 1;
    }
    return lo;
}

/* Position at `key`, or where it would be inserted. */
static Cursor table_find(Table *t, uint32_t key) {
    uint32_t pg = 0;
    uint8_t *n = get_page(t->pager, pg);
    if (!is_leaf(n)) {
        pg = int_child(n, internal_child_index(n, key));
        n = get_page(t->pager, pg);
    }
    Cursor c = {t, pg, leaf_lower_bound(n, key), 0};
    return c;
}

static Cursor table_start(Table *t) {
    uint32_t pg = 0;
    uint8_t *n = get_page(t->pager, pg);
    if (!is_leaf(n)) { pg = int_child(n, 0); n = get_page(t->pager, pg); }
    Cursor c = {t, pg, 0, count(n) == 0};
    return c;
}

static uint8_t *cursor_value(Cursor *c) {
    return leaf_cell(get_page(c->table->pager, c->page), c->cell) + 4;
}

static void cursor_advance(Cursor *c) {
    uint8_t *n = get_page(c->table->pager, c->page);
    if (++c->cell >= count(n)) {
        uint32_t next = rd(n, OFF_NEXT);
        if (next == 0) c->end = 1;
        else { c->page = next; c->cell = 0; }
    }
}

/* ---- insertion ---- */
typedef enum { INS_OK, INS_DUP, INS_FULL } InsResult;

static void leaf_write_cell(uint8_t *leaf, uint32_t i, uint32_t key, const Row *r) {
    wr(leaf_cell(leaf, i), 0, key);
    serialize_row(r, leaf_cell(leaf, i) + 4);
}

/* Insert into non-full leaf. */
static void leaf_insert(uint8_t *leaf, uint32_t pos, uint32_t key, const Row *r) {
    uint32_t n = count(leaf);
    for (uint32_t i = n; i > pos; i--) memcpy(leaf_cell(leaf, i), leaf_cell(leaf, i - 1), CELL_SIZE);
    leaf_write_cell(leaf, pos, key, r);
    set_count(leaf, n + 1);
}

static uint32_t node_max_key(uint8_t *n) { return leaf_key(n, count(n) - 1); }

/* Root leaf is full: move its contents to a new left leaf, make root internal. */
static void split_root(Table *t, uint32_t pos, uint32_t key, const Row *r) {
    Pager *p = t->pager;
    uint8_t *root = get_page(p, 0);
    uint32_t left_pg = new_page(p);
    uint8_t *left = get_page(p, left_pg);
    uint32_t right_pg = new_page(p);
    uint8_t *right = get_page(p, right_pg);

    /* Build the LEAF_MAX_CELLS+1 logical cells, then distribute. */
    uint8_t *tmp = malloc((size_t)(LEAF_MAX_CELLS + 1) * CELL_SIZE);
    for (uint32_t i = 0, j = 0; i <= LEAF_MAX_CELLS; i++) {
        if (i == pos) {
            wr(tmp + (size_t)i * CELL_SIZE, 0, key);
            serialize_row(r, tmp + (size_t)i * CELL_SIZE + 4);
        } else {
            memcpy(tmp + (size_t)i * CELL_SIZE, leaf_cell(root, j++), CELL_SIZE);
        }
    }
    init_leaf(left); init_leaf(right);
    memcpy(leaf_cell(left, 0), tmp, (size_t)LEAF_LEFT_SPLIT * CELL_SIZE);
    set_count(left, LEAF_LEFT_SPLIT);
    memcpy(leaf_cell(right, 0), tmp + (size_t)LEAF_LEFT_SPLIT * CELL_SIZE, (size_t)LEAF_RIGHT_SPLIT * CELL_SIZE);
    set_count(right, LEAF_RIGHT_SPLIT);
    wr(left, OFF_NEXT, right_pg);
    free(tmp);

    memset(root, 0, PAGE_SIZE);
    root[OFF_TYPE] = NODE_INTERNAL;
    root[OFF_ROOT] = 1;
    set_count(root, 1);
    set_int_child(root, 0, left_pg);
    set_int_key(root, 0, node_max_key(left));
    wr(root, OFF_RIGHT, right_pg);
}

/* Full leaf under internal root: split it and register the new sibling. */
static InsResult split_leaf(Table *t, uint32_t leaf_pg, uint32_t pos, uint32_t key, const Row *r) {
    Pager *p = t->pager;
    uint8_t *root = get_page(p, 0);
    if (count(root) >= INTERNAL_MAX_KEYS || p->num_pages + 1 >= TABLE_MAX_PAGES) return INS_FULL;

    uint8_t *old = get_page(p, leaf_pg);
    uint32_t new_pg = new_page(p);
    uint8_t *nw = get_page(p, new_pg);
    old = get_page(p, leaf_pg); /* pointer stays valid (pages are heap blocks) */
    init_leaf(nw);

    uint8_t *tmp = malloc((size_t)(LEAF_MAX_CELLS + 1) * CELL_SIZE);
    for (uint32_t i = 0, j = 0; i <= LEAF_MAX_CELLS; i++) {
        if (i == pos) {
            wr(tmp + (size_t)i * CELL_SIZE, 0, key);
            serialize_row(r, tmp + (size_t)i * CELL_SIZE + 4);
        } else {
            memcpy(tmp + (size_t)i * CELL_SIZE, leaf_cell(old, j++), CELL_SIZE);
        }
    }
    wr(nw, OFF_NEXT, rd(old, OFF_NEXT));
    wr(old, OFF_NEXT, new_pg);
    memcpy(leaf_cell(old, 0), tmp, (size_t)LEAF_LEFT_SPLIT * CELL_SIZE);
    set_count(old, LEAF_LEFT_SPLIT);
    memcpy(leaf_cell(nw, 0), tmp + (size_t)LEAF_LEFT_SPLIT * CELL_SIZE, (size_t)LEAF_RIGHT_SPLIT * CELL_SIZE);
    set_count(nw, LEAF_RIGHT_SPLIT);
    free(tmp);

    /* Find the old leaf's slot in the root, then insert new sibling after it. */
    uint32_t n = count(root), idx = 0;
    while (idx <= n && int_child(root, idx) != leaf_pg) idx++;
    uint32_t old_key_at_idx = idx < n ? int_key(root, idx) : 0;
    /* shift cells idx+1.. right by one */
    for (uint32_t i = n; i > idx; i--) {
        set_int_key(root, i, int_key(root, i - 1));
        wr(root, INTERNAL_HDR + i * INTERNAL_CELL, rd(root, INTERNAL_HDR + (i - 1) * INTERNAL_CELL));
    }
    int was_right = (idx == n);
    uint32_t old_right = rd(root, OFF_RIGHT);
    set_count(root, n + 1);
    set_int_child(root, idx, leaf_pg);
    set_int_key(root, idx, node_max_key(old));
    if (was_right) {
        wr(root, OFF_RIGHT, new_pg);
    } else {
        set_int_child(root, idx + 1, new_pg);
        set_int_key(root, idx + 1, old_key_at_idx);
        wr(root, OFF_RIGHT, old_right);
    }
    return INS_OK;
}

static InsResult table_insert(Table *t, const Row *r) {
    Cursor c = table_find(t, r->id);
    uint8_t *leaf = get_page(t->pager, c.page);
    if (c.cell < count(leaf) && leaf_key(leaf, c.cell) == r->id) return INS_DUP;
    if (count(leaf) < LEAF_MAX_CELLS) { leaf_insert(leaf, c.cell, r->id, r); return INS_OK; }
    if (c.page == 0) { split_root(t, c.cell, r->id, r); return INS_OK; }
    return split_leaf(t, c.page, c.cell, r->id, r);
}

/* ---- debugging view ---- */
static void print_tree(Table *t) {
    uint8_t *root = get_page(t->pager, 0);
    if (is_leaf(root)) {
        printf("- leaf (size %u)\n", count(root));
        for (uint32_t i = 0; i < count(root); i++) printf("  - %u\n", leaf_key(root, i));
        return;
    }
    printf("- internal (size %u)\n", count(root));
    for (uint32_t i = 0; i <= count(root); i++) {
        uint8_t *ch = get_page(t->pager, int_child(root, i));
        printf("  - leaf (size %u)\n", count(ch));
        for (uint32_t j = 0; j < count(ch); j++) printf("    - %u\n", leaf_key(ch, j));
        if (i < count(root)) printf("  - key %u\n", int_key(root, i));
    }
}

/* ---- REPL ---- */
static int do_insert(Table *t, char *line) {
    strtok(line, " ");
    char *id_s = strtok(NULL, " "), *user = strtok(NULL, " "), *email = strtok(NULL, " ");
    if (!id_s || !user || !email) { puts("Syntax error. Could not parse statement."); return 0; }
    char *end;
    long id = strtol(id_s, &end, 10);
    if (*end) { puts("Syntax error. Could not parse statement."); return 0; }
    if (id < 0) { puts("ID must be positive."); return 0; }
    if (strlen(user) > USERNAME_MAX || strlen(email) > EMAIL_MAX) { puts("String is too long."); return 0; }
    Row r = {0};
    r.id = (uint32_t)id;
    strcpy(r.username, user);
    strcpy(r.email, email);
    switch (table_insert(t, &r)) {
    case INS_OK: puts("Executed."); break;
    case INS_DUP: puts("Error: Duplicate key."); break;
    case INS_FULL: puts("Error: Table full."); break;
    }
    return 0;
}

static void do_select(Table *t) {
    for (Cursor c = table_start(t); !c.end; cursor_advance(&c)) {
        Row r;
        deserialize_row(cursor_value(&c), &r);
        printf("(%u, %s, %s)\n", r.id, r.username, r.email);
    }
    puts("Executed.");
}

static void do_find(Table *t, char *line) {
    strtok(line, " ");
    char *id_s = strtok(NULL, " ");
    if (!id_s) { puts("Syntax error. Could not parse statement."); return; }
    uint32_t id = (uint32_t)strtoul(id_s, NULL, 10);
    Cursor c = table_find(t, id);
    uint8_t *leaf = get_page(t->pager, c.page);
    if (c.cell < count(leaf) && leaf_key(leaf, c.cell) == id) {
        Row r;
        deserialize_row(cursor_value(&c), &r);
        printf("(%u, %s, %s)\n", r.id, r.username, r.email);
    }
    puts("Executed.");
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <db file>\n", argv[0]); return 1; }
    Table *t = db_open(argv[1]);
    char *line = NULL;
    size_t cap = 0;
    for (;;) {
        printf("db > ");
        fflush(stdout);
        ssize_t n = getline(&line, &cap, stdin);
        if (n < 0) break; /* EOF acts like .exit so scripted runs persist */
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (line[0] == '.') {
            if (!strcmp(line, ".exit")) break;
            else if (!strcmp(line, ".btree")) { puts("Tree:"); print_tree(t); }
            else if (!strcmp(line, ".constants")) {
                printf("ROW_SIZE: %d\nLEAF_MAX_CELLS: %d\nINTERNAL_MAX_KEYS: %d\n",
                       (int)ROW_SIZE, (int)LEAF_MAX_CELLS, (int)INTERNAL_MAX_KEYS);
            } else printf("Unrecognized command '%s'.\n", line);
        } else if (!strncmp(line, "insert", 6)) do_insert(t, line);
        else if (!strncmp(line, "find", 4)) do_find(t, line);
        else if (!strcmp(line, "select")) do_select(t);
        else printf("Unrecognized keyword at start of '%s'.\n", line);
    }
    free(line);
    db_close(t);
    return 0;
}
