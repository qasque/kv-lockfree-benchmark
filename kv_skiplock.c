#include "kv_common.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define SL_MAX_H 16

typedef struct SLNode {
    uint64_t key;
    char val[KV_VALUE_BYTES];
    int height;
    struct SLNode *next[SL_MAX_H];
} SLNode;

typedef struct {
    SLNode *head;
    CRITICAL_SECTION lock;
} Shard;

struct KVMap {
    int nshards;
    int max_height;
    Shard *shards;
};

static unsigned shard_of(uint64_t k, int ns) {
    return (unsigned)(k % (uint64_t)ns);
}

static _Thread_local unsigned sl_rng;

static int random_level(int max_h) {
    if (sl_rng == 0)
        sl_rng = (unsigned)(GetCurrentThreadId() * 1664525u + 1013904223u);
    int lvl = 1;
    while (lvl < max_h) {
        sl_rng = sl_rng * 1103515245u + 12345u;
        if ((sl_rng >> 16) & 1)
            ++lvl;
        else
            break;
    }
    return lvl;
}

static SLNode *sl_node_new(uint64_t key, const void *val, int h) {
    SLNode *n = (SLNode *)calloc(1, sizeof(SLNode));
    if (!n) return NULL;
    n->key = key;
    memcpy(n->val, val, KV_VALUE_BYTES);
    n->height = h;
    for (int i = 0; i < SL_MAX_H; ++i) n->next[i] = NULL;
    return n;
}

static void sl_free_chain(SLNode *p) {
    while (p) {
        SLNode *nx = p->next[0];
        free(p);
        p = nx;
    }
}

static int sl_get(Shard *s, uint64_t key, void *out) {
    SLNode *x = s->head->next[0];
    while (x && x->key < key) x = x->next[0];
    if (x && x->key == key) {
        memcpy(out, x->val, KV_VALUE_BYTES);
        return 1;
    }
    return 0;
}

static int sl_put(Shard *s, uint64_t key, const void *val, int max_h) {
    SLNode *pred[SL_MAX_H];
    SLNode *x = s->head;
    for (int lv = s->head->height - 1; lv >= 0; --lv) {
        while (x->next[lv] && x->next[lv]->key < key) x = x->next[lv];
        pred[lv] = x;
    }
    x = pred[0]->next[0];
    if (x && x->key == key) {
        memcpy(x->val, val, KV_VALUE_BYTES);
        return 1;
    }
    int nh = random_level(max_h);
    SLNode *n = sl_node_new(key, val, nh);
    if (!n) return 0;
    for (int lv = 0; lv < nh; ++lv) {
        n->next[lv] = pred[lv]->next[lv];
        pred[lv]->next[lv] = n;
    }
    return 1;
}

static int sl_del(Shard *s, uint64_t key) {
    SLNode *pred[SL_MAX_H];
    SLNode *x = s->head;
    for (int lv = s->head->height - 1; lv >= 0; --lv) {
        while (x->next[lv] && x->next[lv]->key < key) x = x->next[lv];
        pred[lv] = x;
    }
    x = pred[0]->next[0];
    if (!x || x->key != key) return 0;
    for (int lv = 0; lv < x->height; ++lv) pred[lv]->next[lv] = x->next[lv];
    free(x);
    return 1;
}

static KVMap *create_impl(int nshards, int max_height) {
    if (nshards < 16) nshards = 256;
    if (max_height < 2) max_height = SL_MAX_H;
    if (max_height > SL_MAX_H) max_height = SL_MAX_H;
    KVMap *m = (KVMap *)calloc(1, sizeof(KVMap));
    if (!m) return NULL;
    m->nshards = nshards;
    m->max_height = max_height;
    m->shards = (Shard *)calloc((size_t)nshards, sizeof(Shard));
    if (!m->shards) {
        free(m);
        return NULL;
    }
    for (int i = 0; i < nshards; ++i) {
        InitializeCriticalSection(&m->shards[i].lock);
        m->shards[i].head = sl_node_new(0, "", max_height);
        if (!m->shards[i].head) {
            for (int j = 0; j < i; ++j) {
                sl_free_chain(m->shards[j].head);
                DeleteCriticalSection(&m->shards[j].lock);
            }
            free(m->shards);
            free(m);
            return NULL;
        }
        m->shards[i].head->key = 0;
        m->shards[i].head->height = max_height;
    }
    return m;
}

static void destroy_impl(KVMap *m) {
    if (!m) return;
    for (int i = 0; i < m->nshards; ++i) {
        sl_free_chain(m->shards[i].head);
        DeleteCriticalSection(&m->shards[i].lock);
    }
    free(m->shards);
    free(m);
}

static int put_impl(KVMap *m, uint64_t key, const void *val) {
    unsigned s = shard_of(key, m->nshards);
    EnterCriticalSection(&m->shards[s].lock);
    int ok = sl_put(&m->shards[s], key, val, m->max_height);
    LeaveCriticalSection(&m->shards[s].lock);
    return ok;
}

static int get_impl(KVMap *m, uint64_t key, void *out) {
    unsigned s = shard_of(key, m->nshards);
    EnterCriticalSection(&m->shards[s].lock);
    int ok = sl_get(&m->shards[s], key, out);
    LeaveCriticalSection(&m->shards[s].lock);
    return ok;
}

static int del_impl(KVMap *m, uint64_t key) {
    unsigned s = shard_of(key, m->nshards);
    EnterCriticalSection(&m->shards[s].lock);
    int ok = sl_del(&m->shards[s], key);
    LeaveCriticalSection(&m->shards[s].lock);
    return ok;
}

const KVMapVTable kv_vtable_skiplock = {
    .create = create_impl,
    .destroy = destroy_impl,
    .put = put_impl,
    .get = get_impl,
    .del = del_impl,
    .name = "fg_skiplock_sharded",
};
