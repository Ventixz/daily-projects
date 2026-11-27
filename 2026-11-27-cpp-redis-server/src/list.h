// Intrusive circular doubly linked list. A node that is not in any list
// points at itself. Used to keep connections in least-recently-active order:
// the head is the one whose idle timeout fires first.
#pragma once

struct DList {
    DList *prev = this;
    DList *next = this;
    DList() = default;
    DList(const DList &) = delete;
    DList &operator=(const DList &) = delete;
};

inline bool dlist_empty(const DList *node) { return node->next == node; }

inline void dlist_detach(DList *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = node->next = node;
}

inline void dlist_insert_before(DList *target, DList *rookie) {
    DList *prev = target->prev;
    prev->next = rookie;
    rookie->prev = prev;
    rookie->next = target;
    target->prev = rookie;
}
