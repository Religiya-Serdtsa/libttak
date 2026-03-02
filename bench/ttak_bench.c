#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <unistd.h>

#include <ttak/mem/mem.h>
#include <ttak/mem/epoch.h>
#include <ttak/mem/epoch_gc.h>
#include <ttak/mem/detachable.h>
#include <ttak/mem/owner.h>

#include <ttak/shared/shared.h>

#include <ttak/thread/pool.h>
#include <ttak/async/sched.h>
#include <ttak/async/task.h>
#include <ttak/async/promise.h>
#include <ttak/async/future.h>

#include <ttak/priority/heap.h>

#include <ttak/atomic/atomic.h>
#include <ttak/sync/sync.h>
#include <ttak/sync/spinlock.h>

#include <ttak/container/pool.h>

#include <ttak/ht/table.h>
#include <ttak/ht/map.h>

#include <ttak/tree/btree.h>
#include <ttak/tree/bplus.h>

#include <ttak/io/bits.h>

#include <ttak/timing/timing.h>

#include <ttak/security/sha256.h>
#include <ttak/math/bigint.h>

#define BENCH_ITERS 100000

/**
 * @brief Prevent the compiler from optimizing away observable variables.
 */
#define KEEP(var) __asm__ volatile("" : : "g"(var) : "memory")

/**
 * @brief Returns resident set size in KB on Linux via /proc.
 */
static long get_rss_kb(void) {
    long rss = 0;
    FILE *fp = fopen("/proc/self/statm", "r");
    if (fp) {
        long resident = 0;
        if (fscanf(fp, "%*s %ld", &resident) == 1) {
            rss = resident * (sysconf(_SC_PAGESIZE) / 1024);
        }
        fclose(fp);
    }
    return rss;
}

/**
 * @brief Prints a single benchmark result line.
 */
static void print_res(const char *name, uint64_t iters, uint64_t ns) {
    double sec = (double)ns / 1e9;
    double ops = (sec > 0.0) ? (double)iters / sec : 0.0;
    double nsp = (iters > 0) ? (double)ns / (double)iters : 0.0;
    printf("  %-38s %10" PRIu64 " ops  %10.0f ops/s  %8.1f ns/op\n", name, iters, ops, nsp);
}

/**
 * @brief Safe unsigned duration between two uint64_t timestamps.
 */
static inline uint64_t dur_u64(uint64_t start, uint64_t end) {
    return (end >= start) ? (end - start) : 0;
}

/**
 * @brief Fast PRNG for generating non-monotonic keys and inputs.
 */
static inline uint64_t xorshift64(uint64_t *s) {
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

/**
 * @brief Hash-like mixing to stabilize observable sinks and reduce dead-code elimination.
 */
static inline uint64_t mix_u64(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

/**
 * @brief Touch memory to ensure allocation is real and data is observable.
 */
static inline void touch_bytes(uint8_t *p, size_t n, uint8_t seed, uint64_t *sink) {
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)(seed + (uint8_t)i);
    }

    uint64_t acc = 0;
    for (size_t i = 0; i < n; i += 8) {
        uint64_t v = 0;
        size_t rem = n - i;
        size_t take = (rem >= 8) ? 8 : rem;
        memcpy(&v, p + i, take);
        acc ^= mix_u64(v + (uint64_t)i);
    }

    *sink ^= acc;
}

/**
 * @brief Comparator for integer-like keys stored as tagged pointers.
 */
static int int_cmp(const void *a, const void *b) {
    intptr_t ia = (intptr_t)a;
    intptr_t ib = (intptr_t)b;
    if (ia == ib) return 0;
    return (ia < ib) ? -1 : 1;
}

/**
 * @brief Comparator for table keys that are 8-byte buffers.
 */
static int u64_deref_cmp(const void *a, const void *b) {
    return memcmp(a, b, 8);
}

/**
 * @brief Initializes a SipHash table and verifies get/put/remove invariants.
 *
 * This function keeps the timestamp 'now' constant across init/put/get/remove,
 * mirroring the rule used by working map tutorial code.
 */
static bool table_init_sanity(ttak_table_t *tbl, size_t capacity, uint64_t now) {
    ttak_table_init(tbl, capacity, NULL, u64_deref_cmp, ttak_mem_free, NULL);

    uint64_t *k = ttak_mem_alloc(8, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!k) {
        ttak_table_destroy(tbl, now);
        return false;
    }

    *k = 0xABCDEF12345678ULL;
    void *expect = (void *)0x1234;

    ttak_table_put(tbl, k, 8, expect, now);

    void *got = ttak_table_get(tbl, k, 8, now);
    if (got != expect) {
        ttak_table_destroy(tbl, now);
        return false;
    }

    (void)ttak_table_remove(tbl, k, 8, now);
    return true;
}

/**
 * @brief Runs the memory allocator microbenchmarks with observability.
 */
static void run_mem(void) {
    puts("\n=== 1. Memory ===");
    const size_t N = BENCH_ITERS;
    uint64_t ts = ttak_get_tick_count();
    uint64_t sink = 0;

    void **ptrs = ttak_mem_alloc(N * sizeof(void *), __TTAK_UNSAFE_MEM_FOREVER__, ts);
    if (!ptrs) {
        fprintf(stderr, "ptrs alloc failed\n");
        return;
    }

    uint64_t t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        ptrs[i] = ttak_mem_alloc(64, __TTAK_UNSAFE_MEM_FOREVER__, ts);
        if (ptrs[i]) {
            touch_bytes((uint8_t *)ptrs[i], 64, (uint8_t)i, &sink);
        } else {
            sink ^= 0xBAD00BAD00BADULL;
        }
    }
    KEEP(sink);
    print_res("mem_alloc (64B)", N, dur_u64(t, ttak_get_tick_count_ns()));

    t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_mem_free(ptrs[i]);
    }
    print_res("mem_free", N, dur_u64(t, ttak_get_tick_count_ns()));

    void *p = ttak_mem_alloc(128, __TTAK_UNSAFE_MEM_FOREVER__, ts);
    if (!p) {
        fprintf(stderr, "mem_access base alloc failed\n");
        ttak_mem_free(ptrs);
        return;
    }
    touch_bytes((uint8_t *)p, 128, 0x5A, &sink);

    t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        void *r = ttak_mem_access(p, ts);
        KEEP(r);
        if (r) sink ^= ((uint8_t *)r)[(i * 31u) & 127u];
    }
    KEEP(sink);
    print_res("mem_access (inline)", N, dur_u64(t, ttak_get_tick_count_ns()));

    ttak_mem_free(p);
    ttak_mem_free(ptrs);
}

/**
 * @brief Runs epoch-based reclamation microbenchmarks.
 */
static void run_epoch(void) {
    puts("\n=== 2. Epoch (EBR) ===");
    const size_t N = BENCH_ITERS;
    uint64_t ts = ttak_get_tick_count();
    uint64_t sink = 0;

    ttak_epoch_register_thread();

    uint64_t t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_epoch_enter();
        sink ^= (uint64_t)i;
        ttak_epoch_exit();
    }
    KEEP(sink);
    print_res("epoch enter/exit", N, dur_u64(t, ttak_get_tick_count_ns()));

    t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_epoch_enter();
        void *obj = ttak_mem_alloc(32, __TTAK_UNSAFE_MEM_FOREVER__, ts);
        if (obj) touch_bytes((uint8_t *)obj, 32, (uint8_t)(i ^ 0xA5u), &sink);
        ttak_epoch_exit();
        if (obj) ttak_epoch_retire(obj, ttak_mem_free);
    }
    KEEP(sink);
    print_res("epoch retire cycle", N, dur_u64(t, ttak_get_tick_count_ns()));

    uint64_t start = ttak_get_tick_count_ns();
    for (int i = 0; i < 3; ++i) ttak_epoch_reclaim();
    print_res("epoch reclaim sweep", 3, dur_u64(start, ttak_get_tick_count_ns()));

    ttak_epoch_gc_t gc;
    ttak_epoch_gc_init(&gc);
    ttak_epoch_gc_manual_rotate(&gc, true);

    t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < 10000; i++) {
        void *obj = ttak_mem_alloc(64, __TTAK_UNSAFE_MEM_FOREVER__, ts);
        if (obj) touch_bytes((uint8_t *)obj, 64, (uint8_t)i, &sink);
        ttak_epoch_gc_register(&gc, obj, 64);
        KEEP(obj);
    }
    KEEP(sink);
    print_res("epoch_gc_register", 10000, dur_u64(t, ttak_get_tick_count_ns()));

    t = ttak_get_tick_count_ns();
    ttak_epoch_gc_rotate(&gc);
    print_res("epoch_gc_rotate", 1, dur_u64(t, ttak_get_tick_count_ns()));

    ttak_epoch_gc_destroy(&gc);
    ttak_epoch_deregister_thread();
}

/**
 * @brief Runs detachable arena microbenchmarks.
 */
static void run_detachable(void) {
    puts("\n=== 3. Detachable Arena ===");
    const size_t N = BENCH_ITERS;
    uint64_t ts = ttak_get_tick_count();
    uint64_t sink = 0;

    ttak_detachable_context_t *ctx = ttak_detachable_context_default();
    ttak_detachable_allocation_t *allocs = ttak_mem_alloc(N * sizeof(*allocs),
                                                          __TTAK_UNSAFE_MEM_FOREVER__, ts);
    if (!ctx || !allocs) {
        fprintf(stderr, "detachable init failed\n");
        return;
    }

    uint64_t t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        allocs[i] = ttak_detachable_mem_alloc(ctx, 128, ts);
        if (allocs[i].data) touch_bytes((uint8_t *)allocs[i].data, 128, (uint8_t)i, &sink);
    }
    KEEP(sink);
    print_res("detachable_alloc (128B)", N, dur_u64(t, ttak_get_tick_count_ns()));

    t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_detachable_mem_free(ctx, &allocs[i]);
    }
    print_res("detachable_free", N, dur_u64(t, ttak_get_tick_count_ns()));

    ttak_mem_free(allocs);
}

/**
 * @brief No-op task body used for thread pool submission timing.
 */
static void *noop_task(void *arg) { return arg; }

/**
 * @brief Task that performs repeated async yields and returns elapsed time.
 */
static void *bench_yield_task(void *arg) {
    const size_t N = (size_t)(intptr_t)arg;
    uint64_t t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_async_yield();
        KEEP(i);
    }
    uint64_t *elapsed = malloc(sizeof(uint64_t));
    if (elapsed) *elapsed = dur_u64(t, ttak_get_tick_count_ns());
    return elapsed;
}

/**
 * @brief Runs async scheduler and thread pool microbenchmarks.
 */
static void run_async(void) {
    puts("\n=== 4. Async/Pool ===");
    const size_t N_POOL = 10000;
    const size_t N_YIELD = 10000;
    uint64_t ts = ttak_get_tick_count();
    uint64_t sink = 0;

    ttak_thread_pool_t *pool = ttak_thread_pool_create(4, 0, ts);
    if (!pool) {
        fprintf(stderr, "thread pool create failed\n");
        return;
    }

    uint64_t t_pool = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N_POOL; i++) {
        ttak_future_t *f = ttak_thread_pool_submit_task(pool, noop_task, (void *)(uintptr_t)i, 0, ts);
        if (f) {
            void *r = ttak_future_get(f);
            sink ^= (uint64_t)(uintptr_t)r;
        } else {
            sink ^= 0x3333333333333333ULL;
        }
    }
    KEEP(sink);
    print_res("pool_submit + future_get", N_POOL, dur_u64(t_pool, ttak_get_tick_count_ns()));

    ttak_thread_pool_destroy(pool);

    ttak_async_init(0);

    ttak_promise_t *promise = ttak_promise_create(ts);
    ttak_future_t *future = promise ? ttak_promise_get_future(promise) : NULL;
    if (!promise || !future) {
        fprintf(stderr, "promise/future init failed\n");
        ttak_async_shutdown();
        return;
    }

    ttak_task_t *task = ttak_task_create(bench_yield_task, (void *)(intptr_t)N_YIELD, promise, ts);
    if (!task) {
        fprintf(stderr, "task create failed\n");
        ttak_async_shutdown();
        return;
    }

    ttak_async_schedule(task, ts, 0);

    uint64_t *res_val = (uint64_t *)ttak_future_get(future);
    if (res_val) {
        print_res("async_yield", N_YIELD, *res_val);
        sink ^= *res_val;
        free(res_val);
    }
    KEEP(sink);

    ttak_task_destroy(task, ts);
    ttak_async_shutdown();
}

/**
 * @brief Runs priority heap microbenchmarks.
 */
static void run_priority(void) {
    puts("\n=== 5. Priority Heap ===");
    const size_t N = BENCH_ITERS;
    uint64_t ts = ttak_get_tick_count();
    uint64_t sink = 0;

    ttak_heap_tree_t heap;
    ttak_heap_tree_init(&heap, 256, int_cmp);

    uint64_t t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_heap_tree_push(&heap, (void *)(intptr_t)i, ts);
    }
    print_res("heap_push", N, dur_u64(t, ttak_get_tick_count_ns()));

    t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        void *v = ttak_heap_tree_pop(&heap, ts);
        sink ^= (uint64_t)(uintptr_t)v;
    }
    KEEP(sink);
    print_res("heap_pop", N, dur_u64(t, ttak_get_tick_count_ns()));

    ttak_heap_tree_destroy(&heap, ts);
}

/**
 * @brief Runs synchronization primitives microbenchmarks.
 */
static void run_sync(void) {
    puts("\n=== 6. Sync/Atomic ===");
    const size_t N = BENCH_ITERS;

    volatile uint64_t counter = 0;
    uint64_t t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_atomic_inc64(&counter);
    }
    KEEP(counter);
    print_res("atomic_inc64", N, dur_u64(t, ttak_get_tick_count_ns()));

    ttak_spin_t spin;
    ttak_spin_init(&spin);
    t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_spin_lock(&spin);
        ttak_spin_unlock(&spin);
    }
    print_res("spinlock lock/unlock", N, dur_u64(t, ttak_get_tick_count_ns()));

    ttak_mutex_t mtx;
    ttak_mutex_init(&mtx);
    t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_mutex_lock(&mtx);
        ttak_mutex_unlock(&mtx);
    }
    print_res("mutex lock/unlock", N, dur_u64(t, ttak_get_tick_count_ns()));

    ttak_mutex_destroy(&mtx);
}

/**
 * @brief Runs container microbenchmarks and validates key invariants.
 *
 * The map operations are executed with a single constant 'now' value across
 * create/insert/get/delete, matching the tutorial behavior exactly.
 */
static void run_containers(void) {
    puts("\n=== 7. Containers ===");
    const size_t N = BENCH_ITERS;
    uint64_t ts0 = ttak_get_tick_count();
    uint64_t sink = 0;

    ttak_object_pool_t *op = ttak_object_pool_create(N, 64);
    void **items = ttak_mem_alloc(N * sizeof(void *), __TTAK_UNSAFE_MEM_FOREVER__, ts0);
    if (!op || !items) {
        fprintf(stderr, "object_pool init failed\n");
        return;
    }

    uint64_t t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        items[i] = ttak_object_pool_alloc(op);
        if (items[i]) touch_bytes((uint8_t *)items[i], 64, (uint8_t)i, &sink);
    }
    KEEP(sink);
    print_res("object_pool_alloc", N, dur_u64(t, ttak_get_tick_count_ns()));

    for (size_t i = 0; i < N; i++) {
        ttak_object_pool_free(op, items[i]);
    }
    ttak_object_pool_destroy(op);
    ttak_mem_free(items);

    {
        uint64_t now = ttak_get_tick_count();
        tt_map_t *map = ttak_create_map(16, now);
        if (!map) {
            fprintf(stderr, "ttak_create_map failed\n");
            return;
        }

        const uintptr_t key = 0xABCDEFu;
        const size_t expect = 99;
        size_t value = 0;

        ttak_insert_to_map(map, key, expect, now);

        _Bool ok = ttak_map_get_key(map, key, &value, now);
        if (!ok || value != expect) {
            fprintf(stderr, "map_sanity failed ok=%d out=%zu expect=%zu\n", (int)ok, value, expect);
            return;
        }

        ttak_delete_from_map(map, key, now);
    }

    {
        const size_t MAP_CAP = 1u << 18;
        uint64_t now = ttak_get_tick_count();

        tt_map_t *map = ttak_create_map(MAP_CAP, now);
        if (!map) {
            fprintf(stderr, "ttak_create_map failed\n");
            return;
        }

        uintptr_t *keys = ttak_mem_alloc(N * sizeof(uintptr_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
        size_t *vals = ttak_mem_alloc(N * sizeof(size_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
        if (!keys || !vals) {
            fprintf(stderr, "map kv alloc failed\n");
            return;
        }

        uint64_t seed = 0xC0FFEE1234ULL ^ (uint64_t)now;

        t = ttak_get_tick_count_ns();
        for (size_t i = 0; i < N; i++) {
            uintptr_t k = (uintptr_t)xorshift64(&seed);
            size_t v = i;

            keys[i] = k;
            vals[i] = v;

            ttak_insert_to_map(map, k, v, now);
            sink ^= mix_u64((uint64_t)k) ^ (uint64_t)v;
        }
        KEEP(sink);
        print_res("map_insert", N, dur_u64(t, ttak_get_tick_count_ns()));

        {
            size_t failures = 0;
            for (size_t i = 0; i < 1024 && i < N; i++) {
                size_t out = 0;
                _Bool ok = ttak_map_get_key(map, keys[i], &out, now);
                if (!ok || out != vals[i]) failures++;
                sink ^= mix_u64((uint64_t)keys[i]) ^ (uint64_t)out;
            }
            KEEP(sink);
            if (failures) fprintf(stderr, "map_verify failures=%zu\n", failures);
        }

        for (size_t i = 0; i < 4096 && i < N; i++) {
            ttak_delete_from_map(map, keys[i], now);
        }

        ttak_mem_free(keys);
        ttak_mem_free(vals);
    }

    {
        ttak_table_t tbl;
        uint64_t now = ttak_get_tick_count();

        if (!table_init_sanity(&tbl, 1024, now)) {
            fprintf(stderr, "table_sanity failed\n");
            return;
        }

        uint64_t **tkeys = ttak_mem_alloc(N * sizeof(uint64_t *), __TTAK_UNSAFE_MEM_FOREVER__, now);
        void **tvals = ttak_mem_alloc(N * sizeof(void *), __TTAK_UNSAFE_MEM_FOREVER__, now);
        if (!tkeys || !tvals) {
            fprintf(stderr, "table kv alloc failed\n");
            ttak_table_destroy(&tbl, now);
            return;
        }

        uint64_t seed = 0xA11CE55EULL ^ (uint64_t)now;

        t = ttak_get_tick_count_ns();
        for (size_t i = 0; i < N; i++) {
            uint64_t *k = ttak_mem_alloc(8, __TTAK_UNSAFE_MEM_FOREVER__, now);
            if (!k) {
                tkeys[i] = NULL;
                tvals[i] = NULL;
                sink ^= 0x6666666666666666ULL;
                continue;
            }

            *k = xorshift64(&seed);
            void *v = (void *)(uintptr_t)i;

            tkeys[i] = k;
            tvals[i] = v;

            ttak_table_put(&tbl, k, 8, v, now);
            sink ^= mix_u64(*k) ^ (uint64_t)i;
        }
        KEEP(sink);
        print_res("table_put (wyhash)", N, dur_u64(t, ttak_get_tick_count_ns()));

        {
            size_t failures = 0;
            for (size_t i = 0; i < 1024 && i < N; i++) {
                if (!tkeys[i]) continue;
                void *v = ttak_table_get(&tbl, tkeys[i], 8, now);
                if (v != tvals[i]) failures++;
                sink ^= (uint64_t)(uintptr_t)v ^ mix_u64(*tkeys[i]);
            }
            KEEP(sink);
            if (failures) fprintf(stderr, "table_verify failures=%zu\n", failures);
        }

        for (size_t i = 0; i < 4096 && i < N; i++) {
            if (!tkeys[i]) continue;
            (void)ttak_table_remove(&tbl, tkeys[i], 8, now);
        }

        ttak_mem_free(tkeys);
        ttak_mem_free(tvals);
        ttak_table_destroy(&tbl, now);
    }
}

/**
 * @brief Runs B-tree and B+tree insertion microbenchmarks.
 */
static void run_trees(void) {
    puts("\n=== 8. Trees ===");
    const size_t N = BENCH_ITERS;
    uint64_t ts = ttak_get_tick_count();
    uint64_t sink = 0;

    ttak_btree_t bt;
    ttak_btree_init(&bt, 4, int_cmp, NULL, NULL);

    uint64_t seed = 0xBADC0DEULL ^ (uint64_t)ts;

    uint64_t t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        void *k = (void *)(intptr_t)(xorshift64(&seed) & 0x7fffffff);
        void *v = (void *)(intptr_t)i;
        ttak_btree_insert(&bt, k, v, ts);
        sink ^= (uint64_t)(uintptr_t)k ^ (uint64_t)(uintptr_t)v;
    }
    KEEP(sink);
    print_res("btree_insert", N, dur_u64(t, ttak_get_tick_count_ns()));
    ttak_btree_destroy(&bt, ts);

    ttak_bplus_tree_t bp;
    ttak_bplus_init(&bp, 4, int_cmp, NULL, NULL);

    seed = 0xFEEDFACEULL ^ (uint64_t)ts;

    t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        void *k = (void *)(intptr_t)(xorshift64(&seed) & 0x7fffffff);
        void *v = (void *)(intptr_t)i;
        ttak_bplus_insert(&bp, k, v, ts);
        sink ^= (uint64_t)(uintptr_t)k + (uint64_t)(uintptr_t)v;
    }
    KEEP(sink);
    print_res("bplus_insert", N, dur_u64(t, ttak_get_tick_count_ns()));
    ttak_bplus_destroy(&bp, ts);
}

/**
 * @brief Runs hashing and time source microbenchmarks.
 */
static void run_io_timing(void) {
    puts("\n=== 9. IO/Timing ===");
    const size_t N = BENCH_ITERS;

    char payload[256];
    memset(payload, 0x55, sizeof(payload));

    uint64_t t = ttak_get_tick_count_ns();
    uint32_t sink = 0;
    for (size_t i = 0; i < N; i++) {
        sink ^= ttak_io_bits_fnv32(payload, sizeof(payload));
    }
    KEEP(sink);
    print_res("fnv32 hash (256B)", N, dur_u64(t, ttak_get_tick_count_ns()));

    t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        volatile uint64_t tick = ttak_get_tick_count_ns();
        KEEP(tick);
    }
    print_res("tick_count_ns", N, dur_u64(t, ttak_get_tick_count_ns()));
}

/**
 * @brief Runs crypto, bigint, and shared ownership microbenchmarks.
 */
static void run_complex(void) {
    puts("\n=== 10. Complex/Shared ===");
    const size_t N = 10000;

    uint8_t data[1024];
    uint8_t hash[SHA256_BLOCK_SIZE];
    memset(data, 0x77, sizeof(data));

    uint64_t sink = 0;
    uint64_t t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        SHA256_CTX ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, data, sizeof(data));
        sha256_final(&ctx, hash);
        sink ^= (uint64_t)hash[(i * 17u) & (SHA256_BLOCK_SIZE - 1u)];
    }
    KEEP(sink);
    print_res("sha256 (1KB)", N, dur_u64(t, ttak_get_tick_count_ns()));

    uint64_t ts = ttak_get_tick_count();
    ttak_bigint_t a, b, c;
    ttak_bigint_init_u64(&a, 0xABCDEF12345678ULL, ts);
    ttak_bigint_init_u64(&b, 0x12345678ABCDEFULL, ts);
    ttak_bigint_init(&c, ts);

    t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_bigint_mul(&c, &a, &b, ts);
    }
    print_res("bigint_mul", N, dur_u64(t, ttak_get_tick_count_ns()));

    ttak_bigint_free(&a, ts);
    ttak_bigint_free(&b, ts);
    ttak_bigint_free(&c, ts);

    ttak_shared_t sh;
    ttak_shared_init(&sh);

    sh.allocate_typed(&sh, 256, "bench", TTAK_SHARED_LEVEL_1);

    ttak_owner_t *owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    sh.add_owner(&sh, owner);

    t = ttak_get_tick_count_ns();
    for (size_t i = 0; i < BENCH_ITERS; i++) {
        ttak_shared_result_t res;
        const void *p = sh.access(&sh, owner, &res);

        sink ^= (uint64_t)res;
        if (p) sink ^= (uint64_t)ttak_shared_get_payload_size(p);

        sh.release(&sh);
    }
    KEEP(sink);
    print_res("shared access/release", BENCH_ITERS, dur_u64(t, ttak_get_tick_count_ns()));

    ttak_owner_destroy(owner);
    ttak_shared_destroy(&sh);
}

/**
 * @brief Entry point for the subsystem benchmark suite.
 */
int main(void) {
    printf("================================================================\n");
    printf("  libttak subsystem bench\n");
    printf("  Initial RSS: %ld KB\n", get_rss_kb());
    printf("================================================================\n");

    run_mem();
    run_epoch();
    run_detachable();
    run_async();
    run_priority();
    run_sync();
    run_containers();
    run_trees();
    run_io_timing();
    run_complex();

    printf("\n  Final RSS:   %ld KB\n", get_rss_kb());
    printf("================================================================\n");

    return 0;
}
