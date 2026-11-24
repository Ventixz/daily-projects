// Intrusive circular doubly linked list with a dummy head.
//
// Used for idle-connection timers: every connection gets the same timeout,
// so moving a connection to the tail on each activity keeps the list sorted
// by deadline, and the head is always the next connection to expire. That is
// O(1) per update, cheaper than a heap.
#pragma once

struct DList {
    DList *prev = this;
    DList *next = this;
};

inline bool dlist_empty(const DList *node) { return node->next == node; }

inline void dlist_detach(DList *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = node->next = node;
}

inline void dlist_insert_before(DList *target, DList *rookie) {
    rookie->prev = target->prev;
    rookie->next = target;
    target->prev->next = rookie;
    target->prev = rookie;
}
