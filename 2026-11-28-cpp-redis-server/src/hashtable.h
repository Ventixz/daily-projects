// Intrusive chained hash map with progressive rehashing.
//
// "Intrusive" means the map never allocates per-item: callers embed an HNode in
// their own struct (by inheriting from it) and the map links those nodes
// together. Getting the outer struct back is a static_cast.
//
// "Progressive rehashing" means a resize never moves every key at once. When the
// load factor is reached, the current table becomes `older_`, a table twice the
// size becomes `newer_`, and every later operation moves a bounded number of
// nodes across. No single request pays for an O(n) resize, which is what keeps
// the server's latency flat while the key count grows.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

struct HNode {
    HNode *next = nullptr;
    uint64_t hcode = 0;
};

struct HTab {
    HNode **tab = nullptr;  // array of slots, each a singly linked chain
    size_t mask = 0;        // capacity - 1; capacity is a power of two
    size_t size = 0;        // number of nodes stored
};

uint64_t hash_bytes(const void *data, size_t len);  // 64-bit FNV-1a

class HMap {
public:
    // `stored` is a node in the map, `key` is the probe passed by the caller.
    using Eq = bool (*)(const HNode *stored, const HNode *key);

    HMap() = default;
    ~HMap();
    HMap(const HMap &) = delete;
    HMap &operator=(const HMap &) = delete;

    HNode *lookup(const HNode *key, Eq eq);
    void insert(HNode *node);  // caller guarantees the key is not present
    HNode *remove(const HNode *key, Eq eq);  // detaches and returns the node
    void clear();  // frees the slot arrays; the nodes belong to the caller

    size_t size() const { return newer_.size + older_.size; }
    bool is_rehashing() const { return older_.tab != nullptr; }
    size_t capacity() const { return newer_.tab ? newer_.mask + 1 : 0; }

    // Calls f(node) for every node until f returns false. f may free the node
    // it was given (the next pointer is read first) but must not touch the map.
    template <class F>
    bool for_each(F &&f) const {
        for (const HTab *t : {&newer_, &older_}) {
            if (!t->tab) continue;
            for (size_t i = 0; i <= t->mask; i++) {
                for (HNode *n = t->tab[i]; n != nullptr;) {
                    HNode *next = n->next;
                    if (!f(n)) return false;
                    n = next;
                }
            }
        }
        return true;
    }

private:
    HTab newer_;
    HTab older_;
    size_t migrate_pos_ = 0;

    void help_rehashing();
    void trigger_rehashing();
};
