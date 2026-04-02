#include "kv_common.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef struct Node {
    uint64_t key;
    char val[KV_VALUE_BYTES];
    struct Node *next;
} Node;

struct KVMap {
    int nbuckets;
    Node **heads;
    CRITICAL_SECTION *locks;
};

static unsigned bucket_ix(uint64_t k, int nb) {
    return (unsigned)(k % (uint64_t)nb);
}

static Node *node_new(uint64_t key, const void *val) {
    Node *n = (Node *)malloc(sizeof(Node));
    if (!n) return NULL;
    n->key = key;
    memcpy(n->val, val, KV_VALUE_BYTES);
    n->next = NULL;
    return n;
}

static KVMap *create_impl(int nbuckets, int unused_max_h) {
    (void)unused_max_h;
    if (nbuckets < 16) nbuckets = 1024;
    KVMap *m = (KVMap *)calloc(1, sizeof(KVMap));
    if (!m) return NULL;
    m->nbuckets = nbuckets;
    m->heads = (Node **)calloc((size_t)nbuckets, sizeof(Node *));
    m->locks = (CRITICAL_SECTION *)malloc((size_t)nbuckets * sizeof(CRITICAL_SECTION));
    if (!m->heads || !m->locks) {
        free(m->heads);
        free(m->locks);
        free(m);
        return NULL;
    }
    for (int i = 0; i < nbuckets; ++i) InitializeCriticalSection(&m->locks[i]);
    return m;
}

static void destroy_impl(KVMap *m) {
    if (!m) return;
    for (int i = 0; i < m->nbuckets; ++i) {
        Node *p = m->heads[i];
        while (p) {
            Node *nx = p->next;
            free(p);
            p = nx;
        }
        DeleteCriticalSection(&m->locks[i]);
    }
    free(m->heads);
    free(m->locks);
    free(m);
}

static int put_impl(KVMap *m, uint64_t key, const void *val) {
    unsigned b = bucket_ix(key, m->nbuckets);
    EnterCriticalSection(&m->locks[b]);
    Node *p = m->heads[b];
    while (p) {
        if (p->key == key) {
            memcpy(p->val, val, KV_VALUE_BYTES);
            LeaveCriticalSection(&m->locks[b]);
            return 1;
        }
        p = p->next;
    }
    Node *n = node_new(key, val);
    if (!n) {
        LeaveCriticalSection(&m->locks[b]);
        return 0;
    }
    n->next = m->heads[b];
    m->heads[b] = n;
    LeaveCriticalSection(&m->locks[b]);
    return 1;
}

static int get_impl(KVMap *m, uint64_t key, void *out) {
    unsigned b = bucket_ix(key, m->nbuckets);
    EnterCriticalSection(&m->locks[b]);
    for (Node *p = m->heads[b]; p; p = p->next) {
        if (p->key == key) {
            memset(out, 0, KV_VALUE_BYTES);
            LeaveCriticalSection(&m->locks[b]);
            return 1;
        }
    }
    LeaveCriticalSection(&m->locks[b]);
    return 0;
}

static int del_impl(KVMap *m, uint64_t key) {
    unsigned b = bucket_ix(key, m->nbuckets);
    EnterCriticalSection(&m->locks[b]);
    Node **pp = &m->heads[b];
    while (*pp) {
        if ((*pp)->key == key) {
            Node *z = *pp;
            *pp = z->next;
            free(z);
            LeaveCriticalSection(&m->locks[b]);
            return 1;
        }
        pp = &(*pp)->next;
    }
    LeaveCriticalSection(&m->locks[b]);
    return 0;
}

const KVMapVTable kv_vtable_wrongget = {
    .create = create_impl,
    .destroy = destroy_impl,
    .put = put_impl,
    .get = get_impl,
    .del = del_impl,
    .name = "wrongget",
};
