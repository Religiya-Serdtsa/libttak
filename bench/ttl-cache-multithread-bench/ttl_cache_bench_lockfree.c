#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <stdint.h>
#include <inttypes.h>

#ifdef _WIN32
#  include <windows.h>
#  include <psapi.h>
#  define bench_sleep_s(s)    Sleep((DWORD)((s) * 1000u))
#  define bench_usleep_us(us) Sleep((DWORD)(((us) + 999) / 1000))
static long get_rss_kb(void) {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (long)(pmc.WorkingSetSize / 1024);
    return 0;
}
#else
#  include <unistd.h>
#  define bench_sleep_s(s)    sleep((unsigned)(s))
#  define bench_usleep_us(us) usleep((useconds_t)(us))
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
#endif

#include <ttak/mem/mem.h>
#include <ttak/mem/epoch.h>
#include <ttak/mem/epoch_gc.h>
#include <ttak/shared/shared.h>
#include <ttak/container/pool.h>
#include <ttak/timing/timing.h>
#include <ttak/thread/pool.h>
#include <ttak/types/ttak_compiler.h>

#if defined(__TINYC__) && defined(__x86_64__)
extern uint64_t g_tsc_scale;
extern void calibrate_tsc(void);
#endif

typedef struct {
    size_t size;
    uint64_t ts;
    uint8_t data[];
} ttak_payload_header_local_t;

#define TTAK_GET_HEADER(ptr) ((ttak_payload_header_local_t *)((uint8_t *)(ptr) - offsetof(ttak_payload_header_local_t, data)))

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
    alignas(64) uint64_t ops;
    alignas(64) uint64_t hits;
    alignas(64) uint64_t swaps;
    alignas(64) uint64_t total_ticks;
} stats_t;

static stats_t stats;
static uint64_t g_running = 1;
static uint64_t g_start_signal = 0;
static uint64_t g_threads_ready = 0;

typedef struct { char data[256]; } cache_payload_t;

static ttak_shared_t *g_cache;
static ttak_epoch_gc_t g_gc;
static ttak_object_pool_t **g_arenas;

static inline uint32_t fast_rand(uint64_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 7;
    *seed ^= *seed << 17;
    return (uint32_t)*seed;
}

#if defined(__TINYC__) && defined(__x86_64__)
static inline uint64_t fast_rdtsc(void) {
    uint32_t low, high;
    __asm__ volatile ("rdtsc" : "=a" (low), "=d" (high));
    return ((uint64_t)high << 32) | low;
}
#else
#define fast_rdtsc() ttak_get_tick_count_ns()
#endif

/**
 * OS-independent memory pre-faulting.
 * Forces physical page allocation by touching every 4KB page.
 */
static void pre_fault_memory(void *ptr, size_t size) {
    volatile uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < size; i += 4096) {
        p[i] = p[i];
    }
}

/**
 * Fast-path execution logic using libttak abstractions.
 */
static void *worker_func(void *arg) {
    ttak_owner_t *owner = (ttak_owner_t *)arg;
    ttak_epoch_register_thread();
    
    uint64_t seed = (uint64_t)(uintptr_t)arg ^ ttak_get_tick_count_ns();
    if (seed == 0) seed = 1;

    /* 
     * PHASE 1: Logic & Cache Warm-up
     * Execute 100,000 operations to prime the CPU and library paths.
     */
    for (int i = 0; i < 100000; i++) {
        ttak_epoch_enter();
        const cache_payload_t *val = (const cache_payload_t *)TTAK_FAST_ATOMIC_LOAD_U64(&g_cache->shared);
        if (TTAK_LIKELY(val)) {
            volatile char c = val->data[0]; (void)c;
        }
        ttak_epoch_exit();
        if (fast_rand(&seed) < (0xFFFFFFFFu / 5)) {
            uint64_t eid = TTAK_FAST_ATOMIC_LOAD_U64(&g_gc.current_epoch);
            void *node = ttak_object_pool_alloc(g_arenas[eid % 4]);
            if (node) ttak_object_pool_free(g_arenas[eid % 4], node);
        }
    }

    /* PHASE 2: Synchronized Barrier */
    TTAK_FAST_ATOMIC_ADD_U64(&g_threads_ready, 1);
    while (TTAK_FAST_ATOMIC_LOAD_U64(&g_start_signal) == 0) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile ("pause");
#endif
    }

    /* PHASE 3: Steady-state Measurement Loop */
    while (TTAK_FAST_ATOMIC_LOAD_U64(&g_running) & 0xFF) {
        uint64_t start = fast_rdtsc();
        
        /* READ: Direct pointer access */
        const cache_payload_t *val = (const cache_payload_t *)TTAK_FAST_ATOMIC_LOAD_U64(&g_cache->shared);

        if (TTAK_LIKELY(val)) {
            volatile char c = val->data[0]; (void)c;
            TTAK_FAST_ATOMIC_ADD_U64(&stats.hits, 1);
        }

        /* UPDATE: Generational swap */
        uint64_t end = fast_rdtsc();
        if (TTAK_UNLIKELY(fast_rand(&seed) < (0xFFFFFFFFu / 5))) {
            uint64_t eid = TTAK_FAST_ATOMIC_LOAD_U64(&g_gc.current_epoch);
            void *node = ttak_object_pool_alloc(g_arenas[eid % 4]);
            
            if (node) {
                /* Prepare payload with user data offset */
                uint8_t *payload_ptr = (uint8_t*)node + 64;
                TTAK_GET_HEADER(payload_ptr)->ts = end; 
                
                /* Perform lock-free pointer exchange */
                void *old_ptr = atomic_exchange_explicit(&g_cache->shared, (void *)payload_ptr, memory_order_acq_rel);
                TTAK_FAST_ATOMIC_ADD_U64(&stats.swaps, 1);
                if (old_ptr) ttak_epoch_retire(old_ptr, NULL); 
            }
        }
        
        TTAK_FAST_ATOMIC_ADD_U64(&stats.ops, 1);
        
        uint64_t ns = end - start;
#if defined(__TINYC__) && defined(__x86_64__)
        ns = (ns * g_tsc_scale) >> 32;
#endif
        TTAK_FAST_ATOMIC_ADD_U64(&stats.total_ticks, ns);
    }

    ttak_epoch_deregister_thread();
    return NULL;
}

/**
 * Control-path: Background resource management.
 */
static void *maintenance_task(void *arg) {
    (void)arg;
    while (TTAK_FAST_ATOMIC_LOAD_U64(&g_start_signal) == 0) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile ("pause");
#endif
    }
    while (TTAK_FAST_ATOMIC_LOAD_U64(&g_running) & 0xFF) {
        bench_usleep_us(100000); 
        ttak_epoch_reclaim();
        ttak_epoch_gc_rotate(&g_gc);
    }
    return NULL;
}

extern void ttak_epoch_subsystem_init(void);

int main(void) {
    /* Eagerly initialize library subsystems to prevent lazy-init overhead */
    ttak_epoch_subsystem_init();
    
#if defined(__TINYC__) && defined(__x86_64__)
    calibrate_tsc();
#endif
    ttak_epoch_gc_init(&g_gc);
    
    /* Calculate required sizes for memory alignment */
    size_t header_size = 64; 
    size_t item_full_size = header_size + sizeof(cache_payload_t);

    g_arenas = malloc(sizeof(ttak_object_pool_t*) * 4);
    for(int i=0; i<4; i++) {
        g_arenas[i] = ttak_object_pool_create(cfg.arena_size / item_full_size, item_full_size);
        
        /* 
         * COMPREHENSIVE PRE-HEATING: 
         * 1. Pre-fault the entire arena memory.
         * 2. Pre-initialize shared memory headers.
         */
        pre_fault_memory(g_arenas[i]->buffer, cfg.arena_size);

        size_t pre_alloc_count = (cfg.arena_size / item_full_size) / 2;
        for(size_t j=0; j < pre_alloc_count; j++) {
            void *raw = ttak_object_pool_alloc(g_arenas[i]);
            if (raw) {
                uint8_t *ptr = (uint8_t*)raw;
                *(size_t*)ptr = sizeof(cache_payload_t);
                *(uint64_t*)(ptr + 8) = ttak_get_tick_count_ns();
                memset(ptr + header_size, 0, sizeof(cache_payload_t));
                ttak_object_pool_free(g_arenas[i], raw);
            }
        }
    }

    g_cache = ttak_mem_alloc(sizeof(ttak_shared_t), 0, ttak_get_tick_count());
    ttak_shared_init(g_cache);
    g_cache->allocate_typed(g_cache, sizeof(cache_payload_t), "cache_payload_t", TTAK_SHARED_NO_LEVEL);
    
    ttak_thread_pool_t *pool = ttak_thread_pool_create(cfg.num_threads + 1, 0, 0);
    
    /* Pre-warm the shard directory and submit tasks */
    for (int i = 0; i < cfg.num_threads; i++) {
        ttak_owner_t *owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
        g_cache->add_owner(g_cache, owner);
        ttak_thread_pool_submit_task(pool, worker_func, owner, 0, 0);
    }
    ttak_thread_pool_submit_task(pool, maintenance_task, NULL, 0, 0);

    /* 
     * WAIT FOR ALL WORKERS TO BE FULLY WARMED UP 
     * This ensures t=1s starts at full velocity.
     */
    while (TTAK_FAST_ATOMIC_LOAD_U64(&g_threads_ready) < (uint64_t)cfg.num_threads) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile ("pause");
#endif
    }

    printf("Time | Ops/s | Latency(ns) | Swaps/s | Epoch | RSS(KB)\n");
    printf("----------------------------------------------------------\n");

    /* Release all threads simultaneously */
    TTAK_FAST_ATOMIC_STORE_BOOL(&g_start_signal, 1);

    for (int i = 1; i <= cfg.duration_sec; i++) {
        bench_sleep_s(1);
        uint64_t ops = TTAK_FAST_ATOMIC_XCHG_U64(&stats.ops, 0);
        uint64_t ticks = TTAK_FAST_ATOMIC_XCHG_U64(&stats.total_ticks, 0);
        uint64_t swaps = TTAK_FAST_ATOMIC_XCHG_U64(&stats.swaps, 0);
        
        uint64_t ns = ticks;
#if defined(__TINYC__) && defined(__x86_64__)
        if (g_tsc_scale == 0) calibrate_tsc();
        ns = (ticks * g_tsc_scale) >> 32;
#endif
        uint64_t lat = (ops > 0) ? (ns / ops) : 0;
        
        long rss = get_rss_kb();

        printf("%2ds | %8" PRIu64 " | %11" PRIu64 " | %7" PRIu64 " | %5" PRIu64 " | %ld\n",
               i, ops, lat, swaps,
               TTAK_FAST_ATOMIC_LOAD_U64(&g_gc.current_epoch),
               rss);
        fflush(stdout);
    }

    TTAK_FAST_ATOMIC_STORE_BOOL(&g_running, 0);
    ttak_thread_pool_destroy(pool);
    return 0;
}
