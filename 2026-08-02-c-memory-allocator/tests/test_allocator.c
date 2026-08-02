#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/allocator.h"

static void test_malloc_returns_usable_memory(void) {
    char *p = my_malloc(16);
    assert(p != NULL);
    strcpy(p, "hello");
    assert(strcmp(p, "hello") == 0);
    my_free(p);
    printf("PASS test_malloc_returns_usable_memory\n");
}

static void test_malloc_zero_returns_null(void) {
    assert(my_malloc(0) == NULL);
    printf("PASS test_malloc_zero_returns_null\n");
}

/* free() on a non-tail block should just flag it free, leaving it in the
 * list for get_free_block() to hand back on the next allocation that fits. */
static void test_free_makes_block_reusable_first_fit(void) {
    size_t base = mem_block_count();

    void *a = my_malloc(64); /* not the tail once b exists */
    void *b = my_malloc(64); /* the tail */
    my_free(a);

    void *c = my_malloc(32); /* should reuse a's block: 64 >= 32 */
    assert(c == a);

    /* Free tail-first (LIFO) so both nodes actually leave the list, instead
     * of leaving a free-but-not-tail node for later tests to trip over. */
    my_free(b);
    my_free(c);
    assert(mem_block_count() == base);
    printf("PASS test_free_makes_block_reusable_first_fit\n");
}

/* free() on the tail block removes it from the list entirely and shrinks
 * the break, rather than leaving a free node behind. */
static void test_free_of_tail_shrinks_list(void) {
    size_t base = mem_block_count();

    void *a = my_malloc(16);
    void *b = my_malloc(16);
    assert(mem_block_count() == base + 2);

    my_free(b); /* b is the tail: node disappears, not just flagged free */
    assert(mem_block_count() == base + 1);

    my_free(a); /* a is now the tail */
    assert(mem_block_count() == base);

    printf("PASS test_free_of_tail_shrinks_list\n");
}

static void test_free_null_is_safe(void) {
    my_free(NULL); /* must not crash */
    printf("PASS test_free_null_is_safe\n");
}

static void test_calloc_zeroes_memory(void) {
    unsigned char *p = my_calloc(10, 4);
    assert(p != NULL);
    for (int i = 0; i < 40; i++) assert(p[i] == 0);
    my_free(p);
    printf("PASS test_calloc_zeroes_memory\n");
}

static void test_calloc_overflow_returns_null(void) {
    assert(my_calloc((size_t)-1, 2) == NULL);
    printf("PASS test_calloc_overflow_returns_null\n");
}

static void test_realloc_grows_and_preserves_data(void) {
    char *p = my_malloc(8);
    strcpy(p, "abcdefg");

    char *q = my_realloc(p, 64);
    assert(q != NULL);
    assert(strcmp(q, "abcdefg") == 0);

    my_free(q);
    printf("PASS test_realloc_grows_and_preserves_data\n");
}

/* Shrinking within the same block's existing capacity must not move it. */
static void test_realloc_shrink_keeps_pointer(void) {
    void *p = my_malloc(64);
    void *q = my_realloc(p, 16);
    assert(q == p);
    my_free(q);
    printf("PASS test_realloc_shrink_keeps_pointer\n");
}

static void test_realloc_null_behaves_like_malloc(void) {
    void *p = my_realloc(NULL, 10);
    assert(p != NULL);
    my_free(p);
    printf("PASS test_realloc_null_behaves_like_malloc\n");
}

static void test_realloc_zero_frees_and_returns_null(void) {
    void *p = my_malloc(10);
    assert(my_realloc(p, 0) == NULL);
    printf("PASS test_realloc_zero_frees_and_returns_null\n");
}

int main(void) {
    test_malloc_returns_usable_memory();
    test_malloc_zero_returns_null();
    test_free_makes_block_reusable_first_fit();
    test_free_of_tail_shrinks_list();
    test_free_null_is_safe();
    test_calloc_zeroes_memory();
    test_calloc_overflow_returns_null();
    test_realloc_grows_and_preserves_data();
    test_realloc_shrink_keeps_pointer();
    test_realloc_null_behaves_like_malloc();
    test_realloc_zero_frees_and_returns_null();
    printf("All tests passed.\n");
    return 0;
}
