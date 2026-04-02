#include "kv_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>

#define MARK_BIT 1u

typedef struct LNode {
    uint64_t key;
    char val[KV_VALUE_BYTES];
    _Atomic uintptr_t next;
} LNode;

struct KVMap {
    int nbuckets;
    LNode **heads;
};

static inline LNode *unwrap_ptr(uintptr_t p) {
    return (LNode *)(p & ~(uintptr_t)MARK_BIT);
}

static inline int is_marked_ref(uintptr_t p) {
    return (p & MARK_BIT) != 0;
}

static inline uintptr_t mark_ref(uintptr_t p) {
    return p | MARK_BIT;
}

static uint64_t mix64(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static unsigned bucket_ix(uint64_t k, int nb) {
    return (unsigned)(mix64(k) % (uint64_t)nb);
}

static LNode *node_new(uint64_t key, const void *val) {
    LNode *n = (LNode *)malloc(sizeof(LNode));
    if (!n) return NULL;
    n->key = key;
    memcpy(n->val, val, KV_VALUE_BYTES);
    atomic_store_explicit(&n->next, (uintptr_t)NULL, memory_order_relaxed);
    return n;
}

static void harris_find(LNode *head, uint64_t key, LNode **pred_out, LNode **curr_out) {
try_again:
    while (1) {
        LNode *pred = head;
        uintptr_t curr_raw = atomic_load_explicit(&pred->next, memory_order_acquire);
        while (1) {
            if (curr_raw == 0) {
                *pred_out = pred;
                *curr_out = NULL;
                return;
            }
            LNode *curr = unwrap_ptr(curr_raw);
            uintptr_t succ_raw = atomic_load_explicit(&curr->next, memory_order_acquire);
            if (is_marked_ref(succ_raw)) {
                uintptr_t succ_unmarked = succ_raw & ~(uintptr_t)MARK_BIT;
                uintptr_t expected = curr_raw;
                if (!atomic_compare_exchange_strong_explicit(
                        &pred->next, &expected, succ_unmarked,
                        memory_order_release, memory_order_relaxed)) {
                    goto try_again;
                }
                curr_raw = atomic_load_explicit(&pred->next, memory_order_acquire);
                continue;
            }
            LNode *succ = unwrap_ptr(succ_raw);
            if (curr->key >= key) {
                *pred_out = pred;
                *curr_out = curr;
                return;
            }
            pred = curr;
            curr_raw = succ_raw;
        }
    }
}

static KVMap *create_impl(int nbuckets, int unused) {
    (void)unused;
    if (nbuckets < 16) nbuckets = 1024;
    KVMap *m = (KVMap *)calloc(1, sizeof(KVMap));
    if (!m) return NULL;
    m->nbuckets = nbuckets;
    m->heads = (LNode **)calloc((size_t)nbuckets, sizeof(LNode *));
    if (!m->heads) {
        free(m);
        return NULL;
    }
    for (int i = 0; i < nbuckets; ++i) {
        LNode *h = node_new(0, "");
        if (!h) {
            for (int j = 0; j < i; ++j) {
                LNode *p = m->heads[j];
                while (p) {
                    LNode *nx = unwrap_ptr(atomic_load_explicit(&p->next, memory_order_relaxed));
                    free(p);
                    p = nx;
                }
            }
            free(m->heads);
            free(m);
            return NULL;
        }
        m->heads[i] = h;
    }
    return m;
}

static void destroy_impl(KVMap *m) {
    if (!m) return;
    for (int i = 0; i < m->nbuckets; ++i) {
        LNode *p = m->heads[i];
        while (p) {
            uintptr_t nr = atomic_load_explicit(&p->next, memory_order_relaxed);
            LNode *nx = unwrap_ptr(nr);
            free(p);
            p = nx;
        }
    }
    free(m->heads);
    free(m);
}

static int put_impl(KVMap *m, uint64_t key, const void *val) {
    unsigned b = bucket_ix(key, m->nbuckets);
    LNode *head = m->heads[b];
    for (;;) {
        LNode *pred, *curr;
        harris_find(head, key, &pred, &curr);
        if (curr && curr->key == key) {
            memcpy(curr->val, val, KV_VALUE_BYTES);
            return 1;
        }
        LNode *n = node_new(key, val);
        if (!n) return 0;
        uintptr_t curr_raw = curr ? (uintptr_t)curr : 0;
        if (atomic_compare_exchange_strong_explicit(&pred->next, &curr_raw, (uintptr_t)n,
                                                    memory_order_release, memory_order_relaxed)) {
            return 1;
        }
        free(n);
    }
}

static int get_impl(KVMap *m, uint64_t key, void *out) {
    unsigned b = bucket_ix(key, m->nbuckets);
    LNode *pred, *curr;
    harris_find(m->heads[b], key, &pred, &curr);
    if (curr && curr->key == key) {
        memcpy(out, curr->val, KV_VALUE_BYTES);
        return 1;
    }
    return 0;
}

static int del_impl(KVMap *m, uint64_t key) {
    unsigned b = bucket_ix(key, m->nbuckets);
    LNode *head = m->heads[b];
    for (;;) {
        LNode *pred, *curr;
        harris_find(head, key, &pred, &curr);
        if (!curr || curr->key != key) return 0;
        uintptr_t succ = atomic_load_explicit(&curr->next, memory_order_acquire);
        if (is_marked_ref(succ)) return 0;
        uintptr_t succ_marked = mark_ref(succ);
        uintptr_t expected = succ;
        if (!atomic_compare_exchange_strong_explicit(&curr->next, &expected, succ_marked,
                                                     memory_order_release, memory_order_relaxed)) {
            continue;
        }
        uintptr_t succ_unmarked = succ;
        uintptr_t curr_raw = (uintptr_t)curr;
        if (atomic_compare_exchange_strong_explicit(&pred->next, &curr_raw, succ_unmarked,
                                                    memory_order_release, memory_order_relaxed)) {
            return 1;
        }
        harris_find(head, key, &pred, &curr);
    }
}

const KVMapVTable kv_vtable_lf_hash = {
    .create = create_impl,
    .destroy = destroy_impl,
    .put = put_impl,
    .get = get_impl,
    .del = del_impl,
    .name = "lf_hash_harris",
};
