/**
 * @file ttak_bench.c
 * @brief Comprehensive libttak performance benchmark.
 *
 * Exercises every major subsystem with timing and throughput measurements:
 *   1. Memory management       (alloc/free, huge pages, cache-aligned, GC)
 *   2. Epoch-Based Reclamation (register/enter/exit/retire/reclaim)
 *   3. Epoch GC                (init/rotate)
 *   4. Detachable arenas       (alloc/free)
 *   5. Threading               (pool submit, promise/future)
 *   6. Async scheduling        (task create/execute, yield)
 *   7. Priority                (heap push/pop, smart scheduler)
 *   8. Atomic counters         (inc64/add64/sub64)
 *   9. Synchronization         (spinlock, mutex, rwlock)
 *  10. Containers              (object pool, ringbuf, set)
 *  11. Hash tables             (table put/get, map insert/get)
 *  12. Trees                   (btree insert/search, bplus insert/get)
 *  13. I/O bits                (FNV hash, verify, recover)
 *  14. Timing / deadline
 *  15. Statistics              (histogram, mean)
 *  16. Rate limiting           (token bucket)
 *  17. SHA-256 hashing
 *  18. BigInt arithmetic       (add, mul, div)
 *  19. Math                    (matrix/vector)
 *  20. Shared ownership + EBR
 *
 * Build:
 *   make -C <root>  # first build libttak.a
 *   gcc -O3 -march=native -std=c17 -pthread -I<root>/include \
 *       bench/ttak_bench.c -o ttak_bench -L<root>/lib -lttak -lm -pthread
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>

/* ---- libttak headers ---- */
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
#include <ttak/priority/scheduler.h>

#include <ttak/atomic/atomic.h>
#include <ttak/sync/sync.h>
#include <ttak/sync/spinlock.h>

#include <ttak/container/pool.h>
#include <ttak/container/ringbuf.h>
#include <ttak/container/set.h>

#include <ttak/ht/table.h>
#include <ttak/ht/map.h>

#include <ttak/tree/btree.h>
#include <ttak/tree/bplus.h>

#include <ttak/io/bits.h>

#include <ttak/timing/timing.h>
#include <ttak/timing/deadline.h>

#include <ttak/stats/stats.h>
#include <ttak/limit/limit.h>

#include <ttak/security/sha256.h>
#include <ttak/math/bigint.h>
#include <ttak/math/vector.h>
#include <ttak/math/matrix.h>

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

#define BENCH_ITERS 100000

static inline uint64_t now_ns(void) { return ttak_get_tick_count_ns(); }
static inline uint64_t now_ms(void) { return ttak_get_tick_count(); }

static long get_rss_kb(void) {
    long rss = 0;
    FILE *fp = fopen("/proc/self/statm", "r");
    if (fp) {
        long resident;
        if (fscanf(fp, "%*s %ld", &resident) == 1)
            rss = resident * (sysconf(_SC_PAGESIZE) / 1024);
        fclose(fp);
    }
    return rss;
}

static void print_bench(const char *name, uint64_t iters, uint64_t elapsed_ns) {
    double sec = (double)elapsed_ns / 1e9;
    double ops = (double)iters / sec;
    double ns_per = (iters > 0) ? (double)elapsed_ns / (double)iters : 0.0;
    printf("  %-36s %10lu ops  %10.0f ops/s  %8.1f ns/op\n",
           name, (unsigned long)iters, ops, ns_per);
}

/* ------------------------------------------------------------------ */
/*  1. Memory management                                              */
/* ------------------------------------------------------------------ */

static void bench_mem(void) {
    puts("\n=== 1. Memory management ===");
    uint64_t t;
    const size_t N = BENCH_ITERS;
    void **ptrs = malloc(N * sizeof(void *));

    /* alloc/free (default) */
    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ptrs[i] = ttak_mem_alloc(64, __TTAK_UNSAFE_MEM_FOREVER__, now_ms());
    uint64_t alloc_ns = now_ns() - t;

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_mem_free(ptrs[i]);
    uint64_t free_ns = now_ns() - t;

    print_bench("mem_alloc (64B default)", N, alloc_ns);
    print_bench("mem_free", N, free_ns);

    /* alloc cache-aligned */
    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ptrs[i] = ttak_mem_alloc_with_flags(256, __TTAK_UNSAFE_MEM_FOREVER__, now_ms(), TTAK_MEM_CACHE_ALIGNED);
    uint64_t ca_ns = now_ns() - t;
    for (size_t i = 0; i < N; i++)
        ttak_mem_free(ptrs[i]);
    print_bench("mem_alloc (256B cache-aligned)", N, ca_ns);

    /* mem_access (lifetime check) */
    void *p = ttak_mem_alloc(128, __TTAK_UNSAFE_MEM_FOREVER__, now_ms());
    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        volatile void *r = ttak_mem_access(p, now_ms());
        (void)r;
    }
    print_bench("mem_access (inline check)", N, now_ns() - t);
    ttak_mem_free(p);

    /* dirty pointer inspection */
    for (size_t i = 0; i < 100; i++)
        ptrs[i] = ttak_mem_alloc(32, 1 /* expire fast */, now_ms());
    usleep(2000);
    size_t dirty_count = 0;
    t = now_ns();
    void **dirty = tt_inspect_dirty_pointers(now_ms(), &dirty_count);
    print_bench("inspect_dirty_pointers", 1, now_ns() - t);
    free(dirty);
    for (size_t i = 0; i < 100; i++)
        ttak_mem_free(ptrs[i]);

    free(ptrs);
}

/* ------------------------------------------------------------------ */
/*  2. Epoch-Based Reclamation                                        */
/* ------------------------------------------------------------------ */

static void dummy_cleanup(void *p) { (void)p; }

static void bench_epoch(void) {
    puts("\n=== 2. Epoch-Based Reclamation ===");
    uint64_t t;
    const size_t N = BENCH_ITERS;

    ttak_epoch_register_thread();

    /* enter/exit cycle */
    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_epoch_enter();
        ttak_epoch_exit();
    }
    print_bench("epoch_enter + epoch_exit", N, now_ns() - t);

    /* retire + reclaim */
    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_epoch_retire(malloc(32), free);
    uint64_t retire_ns = now_ns() - t;
    print_bench("epoch_retire", N, retire_ns);

    t = now_ns();
    ttak_epoch_reclaim();
    print_bench("epoch_reclaim (batch)", 1, now_ns() - t);

    ttak_epoch_deregister_thread();
}

/* ------------------------------------------------------------------ */
/*  3. Epoch GC                                                       */
/* ------------------------------------------------------------------ */

static void bench_epoch_gc(void) {
    puts("\n=== 3. Epoch GC ===");
    uint64_t t;
    const size_t N = 10000;

    ttak_epoch_gc_t gc;
    ttak_epoch_gc_init(&gc);

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_epoch_gc_register(&gc, malloc(64), 64);
    print_bench("epoch_gc_register", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < 100; i++)
        ttak_epoch_gc_rotate(&gc);
    print_bench("epoch_gc_rotate", 100, now_ns() - t);

    ttak_epoch_gc_destroy(&gc);
}

/* ------------------------------------------------------------------ */
/*  4. Detachable arenas                                              */
/* ------------------------------------------------------------------ */

static void bench_detachable(void) {
    puts("\n=== 4. Detachable arenas ===");
    uint64_t t;
    const size_t N = BENCH_ITERS;

    ttak_detachable_context_t *ctx = ttak_detachable_context_default();

    ttak_detachable_allocation_t *allocs = malloc(N * sizeof(*allocs));
    t = now_ns();
    for (size_t i = 0; i < N; i++)
        allocs[i] = ttak_detachable_mem_alloc(ctx, 128, now_ms());
    print_bench("detachable_mem_alloc (128B)", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_detachable_mem_free(ctx, &allocs[i]);
    print_bench("detachable_mem_free", N, now_ns() - t);

    free(allocs);
}

/* ------------------------------------------------------------------ */
/*  5. Threading (pool + promise/future)                              */
/* ------------------------------------------------------------------ */

static void *noop_task(void *arg) { (void)arg; return NULL; }

static void bench_thread_pool(void) {
    puts("\n=== 5. Thread pool + promise/future ===");
    uint64_t t;
    const size_t N = 10000;

    ttak_thread_pool_t *pool = ttak_thread_pool_create(4, 0, now_ms());

    /* submit + future */
    ttak_future_t **futs = malloc(N * sizeof(*futs));
    t = now_ns();
    for (size_t i = 0; i < N; i++)
        futs[i] = ttak_thread_pool_submit_task(pool, noop_task, NULL, 0, now_ms());
    uint64_t submit_ns = now_ns() - t;

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        if (futs[i]) ttak_future_get(futs[i]);
    }
    uint64_t wait_ns = now_ns() - t;

    print_bench("thread_pool_submit_task", N, submit_ns);
    print_bench("future_get (await)", N, wait_ns);

    free(futs);
    ttak_thread_pool_destroy(pool);
}

/* ------------------------------------------------------------------ */
/*  6. Async scheduling                                               */
/* ------------------------------------------------------------------ */

static void *async_noop(void *arg) { (void)arg; return NULL; }

static void bench_async(void) {
    puts("\n=== 6. Async scheduling ===");
    uint64_t t;
    const size_t N = 10000;

    ttak_async_init(0);

    /* task create + schedule */
    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_task_t *task = ttak_task_create(async_noop, NULL, NULL, now_ms());
        ttak_async_schedule(task, now_ms(), 0);
    }
    print_bench("task_create + async_schedule", N, now_ns() - t);

    /* yield */
    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_async_yield();
    print_bench("async_yield", N, now_ns() - t);

    usleep(200000); /* let tasks drain */
    ttak_async_shutdown();
}

/* ------------------------------------------------------------------ */
/*  7. Priority (heap + smart scheduler)                              */
/* ------------------------------------------------------------------ */

static int int_cmp(const void *a, const void *b) {
    intptr_t ia = (intptr_t)a, ib = (intptr_t)b;
    return (ia > ib) - (ia < ib);
}

static void bench_priority(void) {
    puts("\n=== 7. Priority heap + smart scheduler ===");
    uint64_t t;
    const size_t N = BENCH_ITERS;

    ttak_heap_tree_t heap;
    ttak_heap_tree_init(&heap, 256, int_cmp);

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_heap_tree_push(&heap, (void *)(intptr_t)(N - i), now_ms());
    print_bench("heap_push", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_heap_tree_pop(&heap, now_ms());
    print_bench("heap_pop", N, now_ns() - t);

    ttak_heap_tree_destroy(&heap, now_ms());

    /* smart scheduler */
    ttak_scheduler_init();
    ttak_task_t *task = ttak_task_create(async_noop, NULL, NULL, now_ms());
    ttak_task_set_hash(task, 42);

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_scheduler_record_execution(task, 10);
        (void)ttak_scheduler_get_adjusted_priority(task, 5);
    }
    print_bench("scheduler record+adjust", N, now_ns() - t);
    ttak_task_destroy(task, now_ms());
}

/* ------------------------------------------------------------------ */
/*  8. Atomic counters                                                */
/* ------------------------------------------------------------------ */

static void bench_atomic(void) {
    puts("\n=== 8. Atomic counters ===");
    uint64_t t;
    const size_t N = BENCH_ITERS;
    volatile uint64_t counter = 0;

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_atomic_inc64(&counter);
    print_bench("atomic_inc64", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_atomic_add64(&counter, 7);
    print_bench("atomic_add64", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_atomic_sub64(&counter, 3);
    print_bench("atomic_sub64", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_atomic_write64(&counter, i);
        (void)ttak_atomic_read64(&counter);
    }
    print_bench("atomic_write64 + read64", N, now_ns() - t);
}

/* ------------------------------------------------------------------ */
/*  9. Synchronization (spinlock, mutex, rwlock)                      */
/* ------------------------------------------------------------------ */

static void bench_sync(void) {
    puts("\n=== 9. Synchronization primitives ===");
    uint64_t t;
    const size_t N = BENCH_ITERS;

    /* spinlock */
    ttak_spin_t spin;
    ttak_spin_init(&spin);
    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_spin_lock(&spin);
        ttak_spin_unlock(&spin);
    }
    print_bench("spinlock lock+unlock", N, now_ns() - t);

    /* mutex */
    ttak_mutex_t mtx;
    ttak_mutex_init(&mtx);
    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_mutex_lock(&mtx);
        ttak_mutex_unlock(&mtx);
    }
    print_bench("mutex lock+unlock", N, now_ns() - t);
    ttak_mutex_destroy(&mtx);

    /* rwlock (read path) */
    ttak_rwlock_t rw;
    ttak_rwlock_init(&rw);
    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_rwlock_rdlock(&rw);
        ttak_rwlock_unlock(&rw);
    }
    print_bench("rwlock rdlock+unlock", N, now_ns() - t);

    /* rwlock (write path) */
    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_rwlock_wrlock(&rw);
        ttak_rwlock_unlock(&rw);
    }
    print_bench("rwlock wrlock+unlock", N, now_ns() - t);
    ttak_rwlock_destroy(&rw);

    /* backoff */
    ttak_backoff_t bo;
    ttak_backoff_init(&bo);
    t = now_ns();
    for (size_t i = 0; i < 1000; i++)
        ttak_backoff_pause(&bo);
    print_bench("backoff_pause (1k)", 1000, now_ns() - t);
}

/* ------------------------------------------------------------------ */
/* 10. Containers (pool, ringbuf, set)                                */
/* ------------------------------------------------------------------ */

static void bench_containers(void) {
    puts("\n=== 10. Containers ===");
    uint64_t t;
    const size_t N = BENCH_ITERS;

    /* object pool */
    ttak_object_pool_t *opool = ttak_object_pool_create(N, 64);
    void **items = malloc(N * sizeof(void *));

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        items[i] = ttak_object_pool_alloc(opool);
    print_bench("object_pool_alloc (64B)", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_object_pool_free(opool, items[i]);
    print_bench("object_pool_free", N, now_ns() - t);

    ttak_object_pool_destroy(opool);
    free(items);

    /* ringbuf */
    ttak_ringbuf_t *rb = ttak_ringbuf_create(4096, sizeof(uint64_t));

    t = now_ns();
    for (size_t i = 0; i < 4096; i++) {
        uint64_t v = i;
        ttak_ringbuf_push(rb, &v);
    }
    print_bench("ringbuf_push (4096)", 4096, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < 4096; i++) {
        uint64_t v;
        ttak_ringbuf_pop(rb, &v);
    }
    print_bench("ringbuf_pop (4096)", 4096, now_ns() - t);

    ttak_ringbuf_destroy(rb);

    /* set */
    ttak_set_t set;
    ttak_set_init(&set, 1024, NULL, NULL, NULL);

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        uint64_t key = i;
        ttak_set_add(&set, &key, sizeof(key), now_ms());
    }
    print_bench("set_add", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        uint64_t key = i;
        ttak_set_contains(&set, &key, sizeof(key), now_ms());
    }
    print_bench("set_contains", N, now_ns() - t);

    ttak_set_destroy(&set, now_ms());
}

/* ------------------------------------------------------------------ */
/* 11. Hash tables (table, map)                                       */
/* ------------------------------------------------------------------ */

static void bench_hashtables(void) {
    puts("\n=== 11. Hash tables ===");
    uint64_t t;
    const size_t N = BENCH_ITERS;

    /* ttak_table_t (SipHash) */
    ttak_table_t tbl;
    ttak_table_init(&tbl, 1024, NULL, NULL, NULL, NULL);

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        uint64_t key = i;
        ttak_table_put(&tbl, &key, sizeof(key), (void *)(uintptr_t)(i + 1), now_ms());
    }
    print_bench("table_put", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        uint64_t key = i;
        volatile void *v = ttak_table_get(&tbl, &key, sizeof(key), now_ms());
        (void)v;
    }
    print_bench("table_get", N, now_ns() - t);

    ttak_table_destroy(&tbl, now_ms());

    /* ttak_map_t (integer key) */
    ttak_map_t *map = ttak_create_map(1024, now_ms());

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_insert_to_map(map, (uintptr_t)i, i + 1, now_ms());
    print_bench("map_insert", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        size_t val;
        ttak_map_get_key(map, (uintptr_t)i, &val, now_ms());
    }
    print_bench("map_get", N, now_ns() - t);

    /* map_delete */
    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_delete_from_map(map, (uintptr_t)i, now_ms());
    print_bench("map_delete", N, now_ns() - t);

    free(map);
}

/* ------------------------------------------------------------------ */
/* 12. Trees (btree, bplus)                                           */
/* ------------------------------------------------------------------ */

static int ptr_int_cmp(const void *a, const void *b) {
    intptr_t ia = (intptr_t)a, ib = (intptr_t)b;
    return (ia > ib) - (ia < ib);
}

static void bench_trees(void) {
    puts("\n=== 12. Trees ===");
    uint64_t t;
    const size_t N = BENCH_ITERS;

    /* B-tree */
    ttak_btree_t bt;
    ttak_btree_init(&bt, 4, ptr_int_cmp, NULL, NULL);

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_btree_insert(&bt, (void *)(intptr_t)i, (void *)(intptr_t)(i + 1), now_ms());
    print_bench("btree_insert", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        volatile void *v = ttak_btree_search(&bt, (void *)(intptr_t)i, now_ms());
        (void)v;
    }
    print_bench("btree_search", N, now_ns() - t);

    ttak_btree_destroy(&bt, now_ms());

    /* B+ tree */
    ttak_bplus_tree_t bp;
    ttak_bplus_init(&bp, 4, ptr_int_cmp, NULL, NULL);

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_bplus_insert(&bp, (void *)(intptr_t)i, (void *)(intptr_t)(i + 1), now_ms());
    print_bench("bplus_insert", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        volatile void *v = ttak_bplus_get(&bp, (void *)(intptr_t)i, now_ms());
        (void)v;
    }
    print_bench("bplus_get", N, now_ns() - t);

    ttak_bplus_destroy(&bp, now_ms());
}

/* ------------------------------------------------------------------ */
/* 13. I/O bits (FNV hash, verify, recover)                           */
/* ------------------------------------------------------------------ */

static void bench_io_bits(void) {
    puts("\n=== 13. I/O bits ===");
    uint64_t t;
    const size_t N = BENCH_ITERS;

    char payload[256];
    memset(payload, 'A', sizeof(payload));
    uint32_t checksum = ttak_io_bits_fnv32(payload, sizeof(payload));

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_io_bits_fnv32(payload, sizeof(payload));
    print_bench("fnv32 hash (256B)", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_io_bits_verify(payload, sizeof(payload), checksum);
    print_bench("bits_verify (256B)", N, now_ns() - t);

    char dst[256];
    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_io_bits_recover(payload, sizeof(payload), dst, checksum);
    print_bench("bits_recover (256B)", N, now_ns() - t);
}

/* ------------------------------------------------------------------ */
/* 14. Timing / deadline                                              */
/* ------------------------------------------------------------------ */

static void bench_timing(void) {
    puts("\n=== 14. Timing / deadline ===");
    uint64_t t;
    const size_t N = BENCH_ITERS;

    /* tick count */
    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        volatile uint64_t v = ttak_get_tick_count();
        (void)v;
    }
    print_bench("ttak_get_tick_count", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        volatile uint64_t v = ttak_get_tick_count_ns();
        (void)v;
    }
    print_bench("ttak_get_tick_count_ns", N, now_ns() - t);

    /* deadline */
    ttak_deadline_t dl;
    ttak_deadline_set(&dl, 60000); /* 60 s, won't expire */

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        volatile bool exp = ttak_deadline_is_expired(&dl);
        (void)exp;
    }
    print_bench("deadline_is_expired", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        volatile uint64_t rem = ttak_deadline_remaining(&dl);
        (void)rem;
    }
    print_bench("deadline_remaining", N, now_ns() - t);
}

/* ------------------------------------------------------------------ */
/* 15. Statistics                                                     */
/* ------------------------------------------------------------------ */

static void bench_stats(void) {
    puts("\n=== 15. Statistics ===");
    uint64_t t;
    const size_t N = BENCH_ITERS;

    ttak_stats_t st;
    ttak_stats_init(&st, 0, 10000);

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_stats_record(&st, i % 10000);
    print_bench("stats_record", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        volatile double m = ttak_stats_mean(&st);
        (void)m;
    }
    print_bench("stats_mean", N, now_ns() - t);
}

/* ------------------------------------------------------------------ */
/* 16. Rate limiting                                                  */
/* ------------------------------------------------------------------ */

static void bench_ratelimit(void) {
    puts("\n=== 16. Rate limiting ===");
    uint64_t t;
    const size_t N = BENCH_ITERS;

    ttak_token_bucket_t tb;
    ttak_token_bucket_init(&tb, 1e9, 1e9); /* huge burst so it never blocks */

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_token_bucket_consume(&tb, 1.0);
    print_bench("token_bucket_consume", N, now_ns() - t);

    ttak_ratelimit_t rl;
    ttak_ratelimit_init(&rl, 1e9, 1e9);

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_ratelimit_allow(&rl);
    print_bench("ratelimit_allow", N, now_ns() - t);
}

/* ------------------------------------------------------------------ */
/* 17. SHA-256                                                        */
/* ------------------------------------------------------------------ */

static void bench_sha256(void) {
    puts("\n=== 17. SHA-256 ===");
    uint64_t t;
    const size_t N = 10000;

    uint8_t data[1024];
    memset(data, 0x42, sizeof(data));
    uint8_t hash[SHA256_BLOCK_SIZE];

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        SHA256_CTX ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, data, sizeof(data));
        sha256_final(&ctx, hash);
    }
    print_bench("sha256 (1KB)", N, now_ns() - t);
}

/* ------------------------------------------------------------------ */
/* 18. BigInt arithmetic                                              */
/* ------------------------------------------------------------------ */

static void bench_bigint(void) {
    puts("\n=== 18. BigInt arithmetic ===");
    uint64_t t, ts;
    const size_t N = 10000;
    ts = now_ms();

    ttak_bigint_t a, b, c;
    ttak_bigint_init_u64(&a, 999999999ULL, ts);
    ttak_bigint_init_u64(&b, 123456789ULL, ts);
    ttak_bigint_init(&c, ts);

    /* add */
    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_bigint_add(&c, &a, &b, ts);
    print_bench("bigint_add", N, now_ns() - t);

    /* mul */
    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_bigint_mul(&c, &a, &b, ts);
    print_bench("bigint_mul", N, now_ns() - t);

    /* div */
    t = now_ns();
    ttak_bigint_t q, r;
    ttak_bigint_init(&q, ts);
    ttak_bigint_init(&r, ts);
    for (size_t i = 0; i < N; i++)
        ttak_bigint_div(&q, &r, &a, &b, ts);
    print_bench("bigint_div", N, now_ns() - t);

    ttak_bigint_free(&a, ts);
    ttak_bigint_free(&b, ts);
    ttak_bigint_free(&c, ts);
    ttak_bigint_free(&q, ts);
    ttak_bigint_free(&r, ts);
}

/* ------------------------------------------------------------------ */
/* 19. Math (matrix / vector)                                         */
/* ------------------------------------------------------------------ */

static void bench_math(void) {
    puts("\n=== 19. Matrix / Vector ===");
    uint64_t t, ts = now_ms();
    const size_t N = 10000;

    ttak_owner_t *owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);

    /* vector dot product */
    ttak_shared_vector_t *va = ttak_vector_create(3, owner, ts);
    ttak_shared_vector_t *vb = ttak_vector_create(3, owner, ts);

    ttak_bigreal_t one;
    ttak_bigreal_init(&one, ts);
    ttak_bigreal_set_double(&one, 1.0, ts);
    for (uint8_t d = 0; d < 3; d++) {
        ttak_vector_set(va, owner, d, &one, ts);
        ttak_vector_set(vb, owner, d, &one, ts);
    }

    ttak_bigreal_t dot;
    ttak_bigreal_init(&dot, ts);

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_vector_dot(&dot, va, vb, owner, ts);
    print_bench("vector_dot (3D)", N, now_ns() - t);

    ttak_bigreal_t mag;
    ttak_bigreal_init(&mag, ts);
    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_vector_magnitude(&mag, va, owner, ts);
    print_bench("vector_magnitude (3D)", N, now_ns() - t);

    ttak_bigreal_free(&one, ts);
    ttak_bigreal_free(&dot, ts);
    ttak_bigreal_free(&mag, ts);
    ttak_vector_destroy(va, ts);
    ttak_vector_destroy(vb, ts);

    /* matrix multiply */
    ttak_shared_matrix_t *ma = ttak_matrix_create(4, 4, owner, ts);
    ttak_shared_matrix_t *mb = ttak_matrix_create(4, 4, owner, ts);
    ttak_shared_matrix_t *mc = ttak_matrix_create(4, 4, owner, ts);

    ttak_bigreal_t val;
    ttak_bigreal_init(&val, ts);
    ttak_bigreal_set_double(&val, 2.0, ts);
    for (uint8_t r = 0; r < 4; r++)
        for (uint8_t c = 0; c < 4; c++) {
            ttak_matrix_set(ma, owner, r, c, &val, ts);
            ttak_matrix_set(mb, owner, r, c, &val, ts);
        }

    t = now_ns();
    for (size_t i = 0; i < N; i++)
        ttak_matrix_multiply(mc, ma, mb, owner, ts);
    print_bench("matrix_multiply (4x4)", N, now_ns() - t);

    ttak_bigreal_free(&val, ts);
    ttak_matrix_destroy(ma, ts);
    ttak_matrix_destroy(mb, ts);
    ttak_matrix_destroy(mc, ts);

    ttak_owner_destroy(owner);
}

/* ------------------------------------------------------------------ */
/* 20. Shared ownership + EBR                                         */
/* ------------------------------------------------------------------ */

static void bench_shared(void) {
    puts("\n=== 20. Shared ownership + EBR ===");
    uint64_t t, ts = now_ms();
    const size_t N = BENCH_ITERS;

    ttak_shared_t shared;
    ttak_shared_init(&shared);
    shared.allocate_typed(&shared, 256, "bench_payload", TTAK_SHARED_LEVEL_1);

    ttak_owner_t *owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    shared.add_owner(&shared, owner);

    /* access + release */
    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_shared_result_t res;
        const void *p = shared.access(&shared, owner, &res);
        (void)p;
        shared.release(&shared);
    }
    print_bench("shared access+release", N, now_ns() - t);

    /* EBR access + release */
    ttak_epoch_register_thread();
    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_shared_result_t res;
        const void *p = shared.access_ebr(&shared, owner, true, &res);
        (void)p;
        shared.release_ebr(&shared);
    }
    print_bench("shared access_ebr+release_ebr", N, now_ns() - t);
    ttak_epoch_deregister_thread();

    /* swap EBR */
    t = now_ns();
    for (size_t i = 0; i < 1000; i++) {
        void *new_data = malloc(256);
        memset(new_data, (int)(i & 0xFF), 256);
        ttak_shared_swap_ebr(&shared, new_data, 256);
    }
    print_bench("shared swap_ebr", 1000, now_ns() - t);

    ttak_owner_destroy(owner);
    ttak_shared_destroy(&shared);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("================================================================\n");
    printf("  libttak comprehensive bench  (%d iter per micro-bench)\n", BENCH_ITERS);
    printf("  RSS at start: %ld KB\n", get_rss_kb());
    printf("================================================================\n");

    bench_mem();
    bench_epoch();
    bench_epoch_gc();
    bench_detachable();
    bench_thread_pool();
    bench_async();
    bench_priority();
    bench_atomic();
    bench_sync();
    bench_containers();
    bench_hashtables();
    bench_trees();
    bench_io_bits();
    bench_timing();
    bench_stats();
    bench_ratelimit();
    bench_sha256();
    bench_bigint();
    bench_math();
    bench_shared();

    printf("\n================================================================\n");
    printf("  RSS at end: %ld KB\n", get_rss_kb());
    printf("  Done.\n");
    printf("================================================================\n");

    return 0;
}
