/* A first-fit heap allocator built directly on sbrk(2): my_malloc / my_free /
 * my_calloc / my_realloc, each a drop-in for their libc namesakes. Every live
 * or freed block is threaded into one singly-linked list via a header that
 * sits immediately before the memory handed back to the caller. */
#include "allocator.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef union header {
    struct {
        size_t size;
        unsigned is_free;
        union header *next;
    } s;
    /* Padding so the memory after this header starts on a boundary wide
     * enough for any type the caller might store there. */
    long double align;
} header_t;

static header_t *head = NULL, *tail = NULL;
static pthread_mutex_t global_malloc_lock = PTHREAD_MUTEX_INITIALIZER;

static header_t *get_free_block(size_t size) {
    header_t *curr = head;
    while (curr) {
        if (curr->s.is_free && curr->s.size >= size) return curr;
        curr = curr->s.next;
    }
    return NULL;
}

void *my_malloc(size_t size) {
    if (size == 0) return NULL;

    pthread_mutex_lock(&global_malloc_lock);

    header_t *header = get_free_block(size);
    if (header) {
        header->s.is_free = 0;
        pthread_mutex_unlock(&global_malloc_lock);
        return (void *)(header + 1);
    }

    size_t total_size = sizeof(header_t) + size;
    void *block = sbrk((intptr_t)total_size);
    if (block == (void *)-1) {
        pthread_mutex_unlock(&global_malloc_lock);
        return NULL;
    }

    header = block;
    header->s.size = size;
    header->s.is_free = 0;
    header->s.next = NULL;

    if (!head) head = header;
    if (tail) tail->s.next = header;
    tail = header;

    pthread_mutex_unlock(&global_malloc_lock);
    return (void *)(header + 1);
}

void my_free(void *block) {
    if (!block) return;

    pthread_mutex_lock(&global_malloc_lock);

    header_t *header = (header_t *)block - 1;
    void *program_break = sbrk(0);

    if ((char *)block + header->s.size == program_break) {
        /* This is the last block on the heap: hand it back to the OS
         * instead of just flagging it free, so a long-lived program
         * doesn't sit on memory it'll never reuse. Finding the new tail
         * means walking the list from the head, which is the one real
         * inefficiency in this design - see LEARNING.md. */
        if (head == tail) {
            head = tail = NULL;
        } else {
            header_t *tmp = head;
            while (tmp->s.next != tail) tmp = tmp->s.next;
            tmp->s.next = NULL;
            tail = tmp;
        }
        sbrk(0 - (intptr_t)(sizeof(header_t) + header->s.size));
        pthread_mutex_unlock(&global_malloc_lock);
        return;
    }

    header->s.is_free = 1;
    pthread_mutex_unlock(&global_malloc_lock);
}

void *my_calloc(size_t num, size_t nsize) {
    if (num == 0 || nsize == 0) return NULL;
    if (num > (size_t)-1 / nsize) return NULL; /* num * nsize would overflow */

    size_t total = num * nsize;
    void *block = my_malloc(total);
    if (!block) return NULL;

    memset(block, 0, total);
    return block;
}

void *my_realloc(void *block, size_t size) {
    if (!block) return my_malloc(size);
    if (size == 0) {
        my_free(block);
        return NULL;
    }

    header_t *header = (header_t *)block - 1;
    if (header->s.size >= size) return block; /* current block already fits */

    void *new_block = my_malloc(size);
    if (!new_block) return NULL;

    memcpy(new_block, block, header->s.size);
    my_free(block);
    return new_block;
}

void print_mem_list(void) {
    header_t *curr = head;
    printf("head = %p, tail = %p\n", (void *)head, (void *)tail);
    while (curr) {
        printf("addr = %p, size = %zu, is_free = %u, next = %p\n",
               (void *)curr, curr->s.size, curr->s.is_free, (void *)curr->s.next);
        curr = curr->s.next;
    }
}

size_t mem_block_count(void) {
    size_t n = 0;
    for (header_t *curr = head; curr; curr = curr->s.next) n++;
    return n;
}
