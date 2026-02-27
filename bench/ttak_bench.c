/**
 * @file ttak_bench.c
 * @brief Performance and regression test suite for libttak.
 *
 * This benchmark utilizes libttak internal memory allocators to ensure
 * header integrity and prevent metadata corruption during execution.
 */

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

#ifdef _WIN32
#  include <windows.h>
#  include <psapi.h>
#else
#  include <unistd.h>
#endif

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

#define BENCH_ITERS 100000

#if defined(__GNUC__) || defined(__clang__)
/** @brief Prevent compiler from optimizing away the variable */
#  define KEEP(var) __asm__ volatile("" : : "g"(var) : "memory")
#else
#  define KEEP(var) ((void)(&(var)))
#endif

static inline uint64_t now_ns(void) { return ttak_get_tick_count_ns(); }
static inline uint64_t now_ms(void) { return ttak_get_tick_count(); }

/** @brief Retrieve current RSS in KB */
static long get_rss_kb(void) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    return GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) ? (long)(pmc.WorkingSetSize / 1024) : 0;
#else
    long rss = 0;
    FILE *fp = fopen("/proc/self/statm", "r");
    if (fp) {
        long resident;
        if (fscanf(fp, "%*s %ld", &resident) == 1)
            rss = resident * (sysconf(_SC_PAGESIZE) / 1024);
        fclose(fp);
    }
    return rss;
#endif
}

static void print_res(const char *name, uint64_t iters, uint64_t ns) {
    double sec = (double)ns / 1e9;
    printf("  %-38s %10" PRIu64 " ops  %10.0f ops/s  %8.1f ns/op\n",
           name, iters, (sec > 0.0) ? (double)iters / sec : 0.0, (iters > 0) ? (double)ns / (double)iters : 0.0);
}

/** @section 1. Memory management */
static void run_mem(void) {
    puts("\n=== 1. Memory ===");
    const size_t N = BENCH_ITERS;
    uint64_t t, ts = now_ms();
    void **ptrs = ttak_mem_alloc(N * sizeof(void *), __TTAK_UNSAFE_MEM_FOREVER__, ts);

    t = now_ns();
    for (size_t i = 0; i < N; i++) ptrs[i] = ttak_mem_alloc(64, __TTAK_UNSAFE_MEM_FOREVER__, ts);
    print_res("mem_alloc (64B)", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) ttak_mem_free(ptrs[i]);
    print_res("mem_free", N, now_ns() - t);

    void *p = ttak_mem_alloc(128, __TTAK_UNSAFE_MEM_FOREVER__, ts);
    t = now_ns();
    for (size_t i = 0; i < N; i++) { void *r = ttak_mem_access(p, ts); KEEP(r); }
    print_res("mem_access (inline)", N, now_ns() - t);
    ttak_mem_free(p);
    ttak_mem_free(ptrs);
}

/** @section 2. Epoch-Based Reclamation */
static void run_epoch(void) {
    puts("\n=== 2. Epoch (EBR) ===");
    const size_t N = BENCH_ITERS;
    uint64_t ts = now_ms();
    ttak_epoch_register_thread();

    uint64_t t = now_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_epoch_enter();
        KEEP(i);
        ttak_epoch_exit();
    }
    print_res("epoch enter/exit", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_epoch_enter();
        void *obj = ttak_mem_alloc(32, __TTAK_UNSAFE_MEM_FOREVER__, ts);
        KEEP(obj);
        ttak_epoch_exit();
        ttak_epoch_retire(obj, ttak_mem_free);
    }
    print_res("epoch retire cycle", N, now_ns() - t);

    t = now_ns();
    for (int i = 0; i < 3; ++i) ttak_epoch_reclaim();
    print_res("epoch reclaim sweep", 3, now_ns() - t);

    ttak_epoch_gc_t gc;
    ttak_epoch_gc_init(&gc);
    ttak_epoch_gc_manual_rotate(&gc, true);
    t = now_ns();
    for (size_t i = 0; i < 10000; i++) {
        void *obj = ttak_mem_alloc(64, __TTAK_UNSAFE_MEM_FOREVER__, ts);
        ttak_epoch_gc_register(&gc, obj, 64);
        KEEP(obj);
    }
    print_res("epoch_gc_register", 10000, now_ns() - t);

    t = now_ns();
    ttak_epoch_gc_rotate(&gc);
    print_res("epoch_gc_rotate", 1, now_ns() - t);

    ttak_epoch_gc_destroy(&gc);
    ttak_epoch_deregister_thread();
}

/** @section 3. Detachable arena */
static void run_detachable(void) {
    puts("\n=== 3. Detachable Arena ===");
    const size_t N = BENCH_ITERS;
    uint64_t ts = now_ms();
    ttak_detachable_context_t *ctx = ttak_detachable_context_default();
    ttak_detachable_allocation_t *allocs = ttak_mem_alloc(N * sizeof(*allocs), __TTAK_UNSAFE_MEM_FOREVER__, ts);

    uint64_t t = now_ns();
    for (size_t i = 0; i < N; i++) allocs[i] = ttak_detachable_mem_alloc(ctx, 128, ts);
    print_res("detachable_alloc (128B)", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) ttak_detachable_mem_free(ctx, &allocs[i]);
    print_res("detachable_free", N, now_ns() - t);
    ttak_mem_free(allocs);
}

/** @section 4. Async and thread pool */
static void *noop_task(void *arg) { return arg; }
static void run_async(void) {
    puts("\n=== 4. Async/Pool ===");
    const size_t N = 10000;
    uint64_t ts = now_ms();
    ttak_thread_pool_t *pool = ttak_thread_pool_create(4, 0, ts);

    uint64_t t = now_ns();
    for (size_t i = 0; i < N; i++) {
        ttak_future_t *f = ttak_thread_pool_submit_task(pool, noop_task, NULL, 0, ts);
        if (f) { ttak_future_get(f); }
    }
    print_res("pool_submit + future_get", N, now_ns() - t);
    ttak_thread_pool_destroy(pool);

    ttak_async_init(0);
    t = now_ns();
    for (size_t i = 0; i < N; i++) ttak_async_yield();
    print_res("async_yield", N, now_ns() - t);
    ttak_async_shutdown();
}

/** @section 5. Priority and scheduler */
static int int_cmp(const void *a, const void *b) { return (intptr_t)a - (intptr_t)b; }
static void run_priority(void) {
    puts("\n=== 5. Priority Heap ===");
    const size_t N = BENCH_ITERS;
    uint64_t ts = now_ms();
    ttak_heap_tree_t heap;
    ttak_heap_tree_init(&heap, 256, int_cmp);

    uint64_t t = now_ns();
    for (size_t i = 0; i < N; i++) ttak_heap_tree_push(&heap, (void *)(intptr_t)i, ts);
    print_res("heap_push", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) ttak_heap_tree_pop(&heap, ts);
    print_res("heap_pop", N, now_ns() - t);
    ttak_heap_tree_destroy(&heap, ts);
}

/** @section 6. Synchronization */
static void run_sync(void) {
    puts("\n=== 6. Sync/Atomic ===");
    const size_t N = BENCH_ITERS;
    volatile uint64_t counter = 0;

    uint64_t t = now_ns();
    for (size_t i = 0; i < N; i++) ttak_atomic_inc64(&counter);
    print_res("atomic_inc64", N, now_ns() - t);

    ttak_spin_t spin;
    ttak_spin_init(&spin);
    t = now_ns();
    for (size_t i = 0; i < N; i++) { ttak_spin_lock(&spin); ttak_spin_unlock(&spin); }
    print_res("spinlock lock/unlock", N, now_ns() - t);

    ttak_mutex_t mtx;
    ttak_mutex_init(&mtx);
    t = now_ns();
    for (size_t i = 0; i < N; i++) { ttak_mutex_lock(&mtx); ttak_mutex_unlock(&mtx); }
    print_res("mutex lock/unlock", N, now_ns() - t);
    ttak_mutex_destroy(&mtx);
}

/** @section 7. Containers and Hash tables */
static int u64_cmp(const void *a, const void *b) { return memcmp(a, b, 8); }
static void run_containers(void) {
    puts("\n=== 7. Containers ===");
    const size_t N = BENCH_ITERS;
    uint64_t ts = now_ms();

    ttak_object_pool_t *op = ttak_object_pool_create(N, 64);
    void **items = ttak_mem_alloc(N * sizeof(void *), __TTAK_UNSAFE_MEM_FOREVER__, ts);
    uint64_t t = now_ns();
    for (size_t i = 0; i < N; i++) items[i] = ttak_object_pool_alloc(op);
    print_res("object_pool_alloc", N, now_ns() - t);
    for (size_t i = 0; i < N; i++) ttak_object_pool_free(op, items[i]);
    ttak_object_pool_destroy(op);
    ttak_mem_free(items);

    ttak_map_t *map = ttak_create_map(1024, ts);
    t = now_ns();
    for (size_t i = 0; i < N; i++) ttak_insert_to_map(map, (uintptr_t)i, i, ts);
    print_res("map_insert", N, now_ns() - t);
    ttak_mem_free(map);

    ttak_table_t tbl;
    ttak_table_init(&tbl, 1024, NULL, u64_cmp, ttak_mem_free, NULL);
    t = now_ns();
    for (size_t i = 0; i < N; i++) {
        uint64_t *k = ttak_mem_alloc(8, __TTAK_UNSAFE_MEM_FOREVER__, ts);
        *k = i;
        ttak_table_put(&tbl, k, 8, (void *)(uintptr_t)i, ts);
    }
    print_res("table_put (SipHash)", N, now_ns() - t);
    ttak_table_destroy(&tbl, ts);
}

/** @section 8. Tree structures */
static void run_trees(void) {
    puts("\n=== 8. Trees ===");
    const size_t N = BENCH_ITERS;
    uint64_t ts = now_ms();

    ttak_btree_t bt;
    ttak_btree_init(&bt, 4, int_cmp, NULL, NULL);
    uint64_t t = now_ns();
    for (size_t i = 0; i < N; i++) ttak_btree_insert(&bt, (void *)(intptr_t)i, (void *)(intptr_t)i, ts);
    print_res("btree_insert", N, now_ns() - t);
    ttak_btree_destroy(&bt, ts);

    ttak_bplus_tree_t bp;
    ttak_bplus_init(&bp, 4, int_cmp, NULL, NULL);
    t = now_ns();
    for (size_t i = 0; i < N; i++) ttak_bplus_insert(&bp, (void *)(intptr_t)i, (void *)(intptr_t)i, ts);
    print_res("bplus_insert", N, now_ns() - t);
    ttak_bplus_destroy(&bp, ts);
}

/** @section 9. I/O and Timing */
static void run_io_timing(void) {
    puts("\n=== 9. IO/Timing ===");
    const size_t N = BENCH_ITERS;
    char payload[256];
    memset(payload, 0x55, 256);
    uint32_t checksum = ttak_io_bits_fnv32(payload, 256);
    KEEP(checksum);

    uint64_t t = now_ns();
    uint32_t sink = 0;
    for (size_t i = 0; i < N; i++) sink ^= ttak_io_bits_fnv32(payload, 256);
    KEEP(sink);
    print_res("fnv32 hash (256B)", N, now_ns() - t);

    t = now_ns();
    for (size_t i = 0; i < N; i++) { volatile uint64_t tick = ttak_get_tick_count_ns(); KEEP(tick); }
    print_res("tick_count_ns", N, now_ns() - t);
}

/** @section 10. Complex math and shared ownership */
static void run_complex(void) {
    puts("\n=== 10. Complex/Shared ===");
    const size_t N = 10000;
    uint8_t data[1024], hash[SHA256_BLOCK_SIZE];
    memset(data, 0x77, 1024);

    uint64_t t = now_ns();
    for (size_t i = 0; i < N; i++) {
        SHA256_CTX ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, data, 1024);
        sha256_final(&ctx, hash);
        KEEP(hash);
    }
    print_res("sha256 (1KB)", N, now_ns() - t);

    uint64_t ts = now_ms();
    ttak_bigint_t a, b, c;
    ttak_bigint_init_u64(&a, 0xABCDEF12345678ULL, ts);
    ttak_bigint_init_u64(&b, 0x12345678ABCDEFULL, ts);
    ttak_bigint_init(&c, ts);
    t = now_ns();
    for (size_t i = 0; i < N; i++) ttak_bigint_mul(&c, &a, &b, ts);
    print_res("bigint_mul", N, now_ns() - t);
    ttak_bigint_free(&a, ts);
    ttak_bigint_free(&b, ts);
    ttak_bigint_free(&c, ts);

    ttak_shared_t sh;
    ttak_shared_init(&sh);
    sh.allocate_typed(&sh, 256, "bench", TTAK_SHARED_LEVEL_1);
    ttak_owner_t *owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    sh.add_owner(&sh, owner);
    t = now_ns();
    for (size_t i = 0; i < BENCH_ITERS; i++) {
        ttak_shared_result_t res;
        sh.access(&sh, owner, &res);
        sh.release(&sh);
    }
    print_res("shared access/release", BENCH_ITERS, now_ns() - t);
    ttak_owner_destroy(owner);
    ttak_shared_destroy(&sh);
}

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
