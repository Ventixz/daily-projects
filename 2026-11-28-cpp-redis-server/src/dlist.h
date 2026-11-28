// Intrusive circular doubly linked list. The server keeps connections in one,
// ordered by last activity: touching a connection moves it to the back, so the
// front is always the one closest to its idle timeout. That makes the idle
// timer O(1) per event instead of needing a heap.
#pragma once

struct DList {
    DList *prev = this;
    DList *next = this;

    DList() = default;
    DList(const DList &) = delete;
    DList &operator=(const DList &) = delete;

    bool empty() const { return next == this; }

    void detach() {
        prev->next = next;
        next->prev = prev;
        prev = next = this;
    }

    // Insert `node` just before this one. On the list head, that is the back.
    void insert_before(DList *node) {
        DList *p = prev;
        p->next = node;
        node->prev = p;
        node->next = this;
        prev = node;
    }
};
