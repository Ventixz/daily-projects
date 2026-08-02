#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>

void *my_malloc(size_t size);
void my_free(void *block);
void *my_calloc(size_t num, size_t nsize);
void *my_realloc(void *block, size_t size);

void print_mem_list(void);
size_t mem_block_count(void);

#endif
