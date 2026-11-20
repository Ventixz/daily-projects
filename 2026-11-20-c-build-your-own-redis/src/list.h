/* Intrusive circular doubly-linked list with a sentinel head. */
#ifndef LIST_H
#define LIST_H

#include <stdbool.h>

typedef struct DList {
    struct DList *prev, *next;
} DList;

static inline void dlist_init(DList *node) { node->prev = node->next = node; }
static inline bool dlist_empty(const DList *head) { return head->next == head; }

static inline void dlist_detach(DList *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    dlist_init(node);
}

/* Insert `node` just before `target` (before the head = at the tail). */
static inline void dlist_insert_before(DList *target, DList *node) {
    node->prev = target->prev;
    node->next = target;
    target->prev->next = node;
    target->prev = node;
}

#endif
