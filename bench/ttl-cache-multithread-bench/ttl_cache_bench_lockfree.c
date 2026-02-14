#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <unistd.h>

#include <ttak/mem/mem.h>
#include <ttak/mem/epoch.h>
#include <ttak/mem/epoch_gc.h>
#include <ttak/shared/shared.h>
#include <ttak/container/pool.h>
#include <ttak/timing/timing.h>
#include <ttak/thread/pool.h>

/**
 * Benchmark Configuration
 */
typedef struct {
    int num_threads;
    int duration_sec;
    size_t arena_size;
} config_t;

static config_t cfg = { 
    .num_threads = 4, 
    .duration_sec = 10, 
    .arena_size = 1024 * 1024 * 128 /* 128MB Per-epoch Arena */
};

/**
 * Performance Counters:
 * Aligned to 64-byte cache lines to eliminate False Sharing.
 */
typedef struct {
    alignas(64) _Atomic uint_fast64_t ops;
    alignas(64) _Atomic uint_fast64_t hits;
    alignas(64) _Atomic uint_fast64_t swaps;
    alignas(64) _Atomic uint_fast64_t total_ns;
} stats_t;

static stats_t stats;
static atomic_bool g_running = true;

typedef struct { char data[256]; } cache_payload_t;
TTAK_SHARED_DEFINE_WRAPPER(bench, cache_payload_t)

static ttak_shared_bench_t *g_cache;
static ttak_epoch_gc_t g_gc;
static ttak_object_pool_t **g_arenas;

/**
 * Fast-path execution logic using libttak abstractions.
 */
static void *worker_func(void *arg) {
    ttak_owner_t *owner = (ttak_owner_t *)arg;
    ttak_epoch_register_thread();

    while (atomic_load_explicit(&g_running, memory_order_relaxed)) {
        uint64_t start = ttak_get_tick_count_ns();
        ttak_shared_result_t res;
        
        /* READ: EBR-protected pointer access (Zero-lock path) */
        const cache_payload_t *val = (const cache_payload_t *)g_cache->base.access_ebr(
            &g_cache->base, owner, true, &res
        );

        if (val) {
            volatile char c = val->data[0]; (void)c;
            atomic_fetch_add_explicit(&stats.hits, 1, memory_order_relaxed);
            g_cache->base.release_ebr(&g_cache->base);
        }

        /* UPDATE: Generational pointer bumping from pre-allocated pools */
        if (ttak_get_tick_count() % 100 < 20) {
            uint64_t eid = g_gc.current_epoch;
            cache_payload_t *node = (cache_payload_t *)ttak_object_pool_alloc(g_arenas[eid % 4]);
            
            if (node) {
                ttak_shared_swap_ebr(&g_cache->base, node, sizeof(cache_payload_t));
                atomic_fetch_add_explicit(&stats.swaps, 1, memory_order_relaxed);
            }
        }
        
        uint64_t end = ttak_get_tick_count_ns();
        atomic_fetch_add_explicit(&stats.ops, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&stats.total_ns, end - start, memory_order_relaxed);
    }

    ttak_epoch_deregister_thread();
    return NULL;
}

/**
 * Control-path: Background resource management.
 */
static void *maintenance_task(void *arg) {
    (void)arg;
    while (atomic_load(&g_running)) {
        usleep(100000); 
        ttak_epoch_reclaim();
        ttak_epoch_gc_rotate(&g_gc);
    }
    return NULL;
}

int main(void) {
    ttak_epoch_gc_init(&g_gc);
    g_arenas = malloc(sizeof(ttak_object_pool_t*) * 4);
    for(int i=0; i<4; i++) g_arenas[i] = ttak_object_pool_create(cfg.arena_size / 256, 256);

    g_cache = ttak_mem_alloc(sizeof(ttak_shared_bench_t), 0, ttak_get_tick_count());
    ttak_shared_bench_init(g_cache);
    ttak_shared_bench_allocate(g_cache, TTAK_SHARED_LEVEL_1);

    ttak_thread_pool_t *pool = ttak_thread_pool_create(cfg.num_threads + 1, 0, 0);
    
    for (int i = 0; i < cfg.num_threads; i++) {
        ttak_owner_t *owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
        g_cache->base.add_owner(&g_cache->base, owner);
        ttak_thread_pool_submit_task(pool, worker_func, owner, 0, 0);
    }
    ttak_thread_pool_submit_task(pool, maintenance_task, NULL, 0, 0);

    printf("Time | Ops/s | Latency(ns) | Swaps/s | Epoch | RSS(KB)\n");
    printf("----------------------------------------------------------\n");

    for (int i = 1; i <= cfg.duration_sec; i++) {
        sleep(1);
        uint64_t ops = atomic_exchange(&stats.ops, 0);
        uint64_t ns = atomic_exchange(&stats.total_ns, 0);
        uint64_t swaps = atomic_exchange(&stats.swaps, 0);
        uint64_t lat = (ops > 0) ? (ns / ops) : 0;
        
        long rss = 0;
        FILE* fp = fopen("/proc/self/statm", "r");
        if (fp) { fscanf(fp, "%*s %ld", &rss); fclose(fp); rss *= (sysconf(_SC_PAGESIZE)/1024); }

        printf("%2ds | %8lu | %11lu | %7lu | %5lu | %ld\n", i, ops, lat, swaps, g_gc.current_epoch, rss);
    }

    atomic_store(&g_running, false);
    ttak_thread_pool_destroy(pool);
    return 0;
}
