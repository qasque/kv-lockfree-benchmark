#ifndef KV_COMMON_H
#define KV_COMMON_H

#include <stdint.h>

#define KV_VALUE_BYTES 64

typedef enum {
    KV_IMPL_MUTEX = 0,
    KV_IMPL_RWLOCK,
    KV_IMPL_SKIPLOCK,
    KV_IMPL_LF_HASH,
    KV_IMPL_LF_SKIP,
    KV_IMPL_COUNT
} KVImplId;

typedef struct KVMap KVMap;

typedef struct {
    KVMap *(*create)(int buckets_or_shards, int max_height);
    void (*destroy)(KVMap *m);
    int (*put)(KVMap *m, uint64_t key, const void *val);
    int (*get)(KVMap *m, uint64_t key, void *out);
    int (*del)(KVMap *m, uint64_t key);
    const char *name;
} KVMapVTable;

extern const KVMapVTable kv_vtable_mutex;
extern const KVMapVTable kv_vtable_rwlock;
extern const KVMapVTable kv_vtable_skiplock;
extern const KVMapVTable kv_vtable_lf_hash;
extern const KVMapVTable kv_vtable_lf_skip;
extern const KVMapVTable kv_vtable_wrongget;

#endif
