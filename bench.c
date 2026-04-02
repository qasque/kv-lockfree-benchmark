#include "bench.h"
#include "kv_common.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

typedef struct {
    _Atomic uint32_t verify_errors;
    _Atomic uint64_t ops_live;
} BenchShared;

typedef struct {
    const KVMapVTable *vt;
    KVMap *map;
    int tid;
    uint64_t duration_ticks;
    uint64_t keyspace;
    int profile;
    int use_zipf;
    double zipf_s;
    uint64_t *lat_samples;
    size_t lat_cap;
    size_t lat_written;
    uint64_t *out_ops;
    BenchShared *shared;
} ThreadCtx;

static uint64_t qpc_freq(void) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (uint64_t)f.QuadPart;
}

static uint64_t qpc_now(void) {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint64_t)c.QuadPart;
}

static _Thread_local uint64_t rng_state;

static void rng_init(int tid) {
    rng_state = 0x9E3779B97F4A7C15ULL ^ (uint64_t)(unsigned)tid * 14695981039346656037ULL;
}

static uint64_t rng_u64(void) {
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x * 2685821657736338717ULL;
    return x;
}

static uint64_t key_uniform(uint64_t keyspace) {
    if (keyspace <= 1) return 1;
    return (rng_u64() % (keyspace - 1)) + 1;
}

static double zipf_sample(uint64_t n, double s) {
    if (n <= 1) return 1.0;
    double u = (double)(rng_u64() % 1000000) / 1000000.0;
    double sum = 0.0;
    for (uint64_t k = 1; k <= n; ++k)
        sum += 1.0 / pow((double)k, s);
    double r = u * sum;
    double acc = 0.0;
    for (uint64_t k = 1; k <= n; ++k) {
        acc += 1.0 / pow((double)k, s);
        if (r <= acc) return (double)k;
    }
    return (double)n;
}

static uint64_t key_zipf(uint64_t keyspace, double s) {
    double z = zipf_sample(keyspace, s);
    uint64_t k = (uint64_t)(z + 0.5);
    if (k < 1) k = 1;
    if (k >= keyspace) k = keyspace - 1;
    return k;
}

static uint64_t next_key(ThreadCtx *ctx) {
    if (ctx->use_zipf)
        return key_zipf(ctx->keyspace, ctx->zipf_s);
    return key_uniform(ctx->keyspace);
}

static void val_encode(uint64_t key, void *buf) {
    memset(buf, 0, KV_VALUE_BYTES);
    uint64_t v = key ^ 0x9E3779B97F4A7C15ULL;
    memcpy(buf, &v, sizeof(v));
}

static int val_check(uint64_t key, const void *buf) {
    uint64_t expect, got;
    expect = key ^ 0x9E3779B97F4A7C15ULL;
    memcpy(&got, buf, sizeof(got));
    return got == expect ? 0 : 1;
}

static DWORD WINAPI worker(LPVOID p) {
    ThreadCtx *ctx = (ThreadCtx *)p;
    rng_init(ctx->tid);
    const KVMapVTable *vt = ctx->vt;
    KVMap *m = ctx->map;
    uint64_t end = qpc_now() + ctx->duration_ticks;
    uint64_t freq = qpc_freq();
    size_t sample_every = 97;
    uint64_t op_n = 0;
    size_t lat_idx = 0;

    while (qpc_now() < end) {
        uint64_t k = next_key(ctx);
        char val[KV_VALUE_BYTES];
        char out[KV_VALUE_BYTES];
        uint64_t t0 = qpc_now();
        int op;
        uint64_t r = rng_u64() % 100;

        switch (ctx->profile) {
        case 0:
            if (r < 90) op = 1;
            else if (r < 98) op = 0;
            else op = 2;
            break;
        case 1:
            if (r < 50) op = 1;
            else if (r < 85) op = 0;
            else op = 2;
            break;
        case 2:
            if (r < 20) op = 1;
            else if (r < 80) op = 0;
            else op = 2;
            break;
        default:
            if (r < 40) op = 1;
            else if (r < 70) op = 0;
            else op = 2;
            break;
        }

        if (op == 0) {
            val_encode(k, val);
            vt->put(m, k, val);
        } else if (op == 1) {
            if (vt->get(m, k, out)) {
                if (val_check(k, out))
                    atomic_fetch_add_explicit(&ctx->shared->verify_errors, 1u, memory_order_relaxed);
            }
        } else {
            vt->del(m, k);
        }

        atomic_fetch_add_explicit(&ctx->shared->ops_live, 1, memory_order_relaxed);

        uint64_t t1 = qpc_now();
        uint64_t dt_ns = (t1 > t0) ? (uint64_t)((double)(t1 - t0) * 1e9 / (double)freq) : 0;
        op_n++;
        if ((op_n % sample_every) == 0 && ctx->lat_samples && lat_idx < ctx->lat_cap)
            ctx->lat_samples[lat_idx++] = dt_ns;
    }
    if (ctx->out_ops)
        *ctx->out_ops = op_n;
    ctx->lat_written = lat_idx;
    return 0;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static uint64_t percentile_ns(uint64_t *arr, size_t n, double p) {
    if (n == 0) return 0;
    qsort(arr, n, sizeof(uint64_t), cmp_u64);
    size_t idx = (size_t)((double)(n - 1) * p);
    return arr[idx];
}

static size_t process_memory_bytes(void) {
    PROCESS_MEMORY_COUNTERS pmc;
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return 0;
    return (size_t)pmc.WorkingSetSize;
}

static const KVMapVTable *vt_by_name(const char *name) {
    if (!strcmp(name, "mutex")) return &kv_vtable_mutex;
    if (!strcmp(name, "rwlock")) return &kv_vtable_rwlock;
    if (!strcmp(name, "skiplock")) return &kv_vtable_skiplock;
    if (!strcmp(name, "lfhash")) return &kv_vtable_lf_hash;
    if (!strcmp(name, "lfskip")) return &kv_vtable_lf_skip;
    if (!strcmp(name, "wrongget")) return &kv_vtable_wrongget;
    return NULL;
}

static int profile_id(const char *s) {
    if (!strcmp(s, "readheavy")) return 0;
    if (!strcmp(s, "mixed")) return 1;
    if (!strcmp(s, "writeheavy")) return 2;
    if (!strcmp(s, "churn")) return 3;
    return 1;
}

static void usage(FILE *f) {
    fprintf(f,
            "bench [--verify-selftest] | --impl mutex|rwlock|skiplock|lfhash|lfskip|wrongget "
            "[--threads N] [--seconds S] [--profile readheavy|mixed|writeheavy|churn] "
            "[--keyspace K] [--buckets B] [--zipf 0|1] [--zipf-s 1.2] [--seed N] [--csv-header] "
            "[--snapshot-interval-sec N] [--snapshot-file path] [--snapshot-csv-header]\n");
}

int bench_main(int argc, char **argv) {
    const char *impl = "mutex";
    int threads = 8;
    int seconds = 5;
    const char *profile_s = "mixed";
    uint64_t keyspace = 1000000u;
    int buckets = 1024;
    int use_zipf = 0;
    double zipf_s = 1.0001;
    uint64_t seed_count = 0;
    int csv_header = 0;
    int snapshot_interval_sec = 0;
    const char *snapshot_file = NULL;
    int snapshot_csv_header = 0;
    int verify_selftest = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--impl") && i + 1 < argc)
            impl = argv[++i];
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc)
            threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--profile") && i + 1 < argc)
            profile_s = argv[++i];
        else if (!strcmp(argv[i], "--keyspace") && i + 1 < argc)
            keyspace = (uint64_t)strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--buckets") && i + 1 < argc)
            buckets = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--zipf") && i + 1 < argc)
            use_zipf = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--zipf-s") && i + 1 < argc)
            zipf_s = atof(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed_count = (uint64_t)strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--csv-header"))
            csv_header = 1;
        else if (!strcmp(argv[i], "--snapshot-interval-sec") && i + 1 < argc)
            snapshot_interval_sec = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--snapshot-file") && i + 1 < argc)
            snapshot_file = argv[++i];
        else if (!strcmp(argv[i], "--snapshot-csv-header"))
            snapshot_csv_header = 1;
        else if (!strcmp(argv[i], "--verify-selftest"))
            verify_selftest = 1;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(stdout);
            return 0;
        }
    }

    if (threads < 1) threads = 1;
    if (threads > 64) threads = 64;
    if (seconds < 1) seconds = 1;
    if (keyspace < 2) keyspace = 2;
    if (buckets < 16) buckets = 16;
    if (snapshot_interval_sec < 0) snapshot_interval_sec = 0;
    if (snapshot_interval_sec > 0 && snapshot_file == NULL)
        snapshot_file = "snapshots.csv";

    if (verify_selftest) {
        char okbuf[KV_VALUE_BYTES];
        val_encode(999u, okbuf);
        if (val_check(999u, okbuf) != 0) {
            fprintf(stderr, "verify-selftest: FAILED (expected match for encoded value)\n");
            return 2;
        }
        char badbuf[KV_VALUE_BYTES];
        memset(badbuf, 0, sizeof(badbuf));
        if (val_check(12345u, badbuf) == 0) {
            fprintf(stderr, "verify-selftest: FAILED (expected mismatch for zero buffer)\n");
            return 2;
        }
        BenchShared sh;
        atomic_store_explicit(&sh.verify_errors, 0u, memory_order_relaxed);
        if (val_check(12345u, badbuf))
            atomic_fetch_add_explicit(&sh.verify_errors, 1u, memory_order_relaxed);
        uint32_t v = atomic_load_explicit(&sh.verify_errors, memory_order_relaxed);
        if (v != 1u) {
            fprintf(stderr, "verify-selftest: FAILED (atomic counter: expected 1, got %u)\n", v);
            return 2;
        }
        fputs("verify-selftest: OK (value check and atomic error counter)\n", stdout);
        return 0;
    }

    const KVMapVTable *vt = vt_by_name(impl);
    if (!vt) {
        fprintf(stderr, "unknown --impl %s\n", impl);
        usage(stderr);
        return 1;
    }

    int prof = profile_id(profile_s);
    uint64_t freq = qpc_freq();
    uint64_t duration_ticks = (uint64_t)seconds * freq;

    KVMap *map = vt->create(buckets, 16);
    if (!map) {
        fprintf(stderr, "map create failed\n");
        return 1;
    }
    char val[KV_VALUE_BYTES];
    if (seed_count > 0) {
        if (seed_count >= keyspace) seed_count = keyspace - 1;
        for (uint64_t k = 1; k <= seed_count; ++k) {
            val_encode(k, val);
            vt->put(map, k, val);
        }
    }

    size_t mem_before = process_memory_bytes();

    BenchShared shared;
    atomic_store_explicit(&shared.verify_errors, 0u, memory_order_relaxed);
    atomic_store_explicit(&shared.ops_live, 0, memory_order_relaxed);

    FILE *snap_fp = NULL;
    if (snapshot_interval_sec > 0 && snapshot_file) {
        snap_fp = fopen(snapshot_file, "wb");
        if (!snap_fp) {
            fprintf(stderr, "cannot open snapshot file: %s\n", snapshot_file);
            vt->destroy(map);
            return 1;
        }
        if (snapshot_csv_header)
            fprintf(snap_fp,
                    "impl,threads,profile,keyspace,t_elapsed_sec,ops_cumulative,ops_per_sec_window,mem_ws_bytes,verify_errors\n");
    }

    HANDLE *th = (HANDLE *)calloc((size_t)threads, sizeof(HANDLE));
    ThreadCtx *ctxs = (ThreadCtx *)calloc((size_t)threads, sizeof(ThreadCtx));
    uint64_t *ops_each = (uint64_t *)calloc((size_t)threads, sizeof(uint64_t));
    size_t per_thread_lat = 50000;
    uint64_t *lat_all = (uint64_t *)calloc((size_t)threads * per_thread_lat, sizeof(uint64_t));
    if (!th || !ctxs || !ops_each || !lat_all) {
        fprintf(stderr, "alloc failed\n");
        if (snap_fp)
            fclose(snap_fp);
        free(th);
        free(ctxs);
        free(ops_each);
        free(lat_all);
        vt->destroy(map);
        return 1;
    }
    for (int t = 0; t < threads; ++t) {
        ctxs[t].vt = vt;
        ctxs[t].map = map;
        ctxs[t].tid = t;
        ctxs[t].duration_ticks = duration_ticks;
        ctxs[t].keyspace = keyspace;
        ctxs[t].profile = prof;
        ctxs[t].use_zipf = use_zipf;
        ctxs[t].zipf_s = zipf_s;
        ctxs[t].lat_samples = lat_all + (size_t)t * per_thread_lat;
        ctxs[t].lat_cap = per_thread_lat;
        ctxs[t].lat_written = 0;
        ctxs[t].out_ops = &ops_each[t];
        ctxs[t].shared = &shared;
        th[t] = CreateThread(NULL, 0, worker, &ctxs[t], 0, NULL);
        if (!th[t]) {
            fprintf(stderr, "CreateThread failed\n");
            if (snap_fp)
                fclose(snap_fp);
            for (int j = 0; j < t; ++j) CloseHandle(th[j]);
            free(th);
            free(ctxs);
            free(ops_each);
            free(lat_all);
            vt->destroy(map);
            return 1;
        }
    }
    uint64_t snap_t0 = qpc_now();
    uint64_t snap_last_t = snap_t0;
    uint64_t snap_last_ops = 0;

    if (snapshot_interval_sec > 0 && snap_fp) {
        DWORD interval_ms = (DWORD)(snapshot_interval_sec * 1000);
        if (interval_ms == 0) interval_ms = 1000;
        for (;;) {
            DWORD w = WaitForMultipleObjects((DWORD)threads, th, TRUE, interval_ms);
            uint64_t tnow = qpc_now();
            uint64_t ops_now = atomic_load_explicit(&shared.ops_live, memory_order_relaxed);
            uint32_t verr_now = atomic_load_explicit(&shared.verify_errors, memory_order_relaxed);
            size_t mem_now = process_memory_bytes();
            double dt_win = (tnow > snap_last_t) ? ((double)(tnow - snap_last_t) / (double)freq) : 0.0;
            double t_el = (tnow > snap_t0) ? ((double)(tnow - snap_t0) / (double)freq) : 0.0;
            uint64_t dops = ops_now - snap_last_ops;
            double ops_ps_win = (dt_win > 0.0) ? ((double)dops / dt_win) : 0.0;
            fprintf(snap_fp, "%s,%d,%s,%llu,%.3f,%llu,%.2f,%zu,%u\n", impl, threads, profile_s,
                    (unsigned long long)keyspace, t_el, (unsigned long long)ops_now, ops_ps_win, mem_now, verr_now);
            fflush(snap_fp);
            snap_last_t = tnow;
            snap_last_ops = ops_now;
            if (w != WAIT_TIMEOUT)
                break;
        }
    } else {
        WaitForMultipleObjects((DWORD)threads, th, TRUE, INFINITE);
    }

    for (int t = 0; t < threads; ++t) CloseHandle(th[t]);
    free(th);
    if (snap_fp)
        fclose(snap_fp);

    uint64_t total_ops = 0;
    for (int t = 0; t < threads; ++t) total_ops += ops_each[t];

    size_t merged_n = 0;
    uint64_t *merged = (uint64_t *)malloc((size_t)threads * per_thread_lat * sizeof(uint64_t));
    if (merged) {
        for (int t = 0; t < threads; ++t) {
            for (size_t i = 0; i < ctxs[t].lat_written && i < per_thread_lat; ++i)
                merged[merged_n++] = lat_all[(size_t)t * per_thread_lat + i];
        }
    }

    uint64_t p99 = 0;
    if (merged && merged_n > 0)
        p99 = percentile_ns(merged, merged_n, 0.99);
    free(merged);

    size_t mem_after = process_memory_bytes();
    long long mem_delta = (long long)mem_after - (long long)mem_before;

    double sec = (double)seconds;
    double ops_per_sec = sec > 0 ? (double)total_ops / sec : 0.0;

    uint64_t verr = (uint64_t)atomic_load_explicit(&shared.verify_errors, memory_order_relaxed);

    if (csv_header)
        printf("impl,threads,profile,seconds,keyspace,ops_total,ops_per_sec,mem_ws_before,mem_ws_after,mem_delta_ws,p99_ns,verify_errors\n");

    printf("%s,%d,%s,%d,%llu,%llu,%.2f,%zu,%zu,%lld,%llu,%llu\n", impl, threads, profile_s, seconds,
           (unsigned long long)keyspace, (unsigned long long)total_ops, ops_per_sec, mem_before, mem_after,
           (long long)mem_delta, (unsigned long long)p99, (unsigned long long)verr);

    free(lat_all);
    free(ops_each);
    free(ctxs);
    vt->destroy(map);
    return 0;
}
