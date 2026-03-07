#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include <limits.h>

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

#if defined(__x86_64__) || defined(__i386__)
#define BENCH_HAS_NATIVE_TSC 1
#if defined(__TINYC__)
static inline uint64_t bench_read_cycles(void) {
    uint32_t low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}
#else
static inline uint64_t bench_read_cycles(void) {
    uint32_t low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}
#endif

static inline uint64_t bench_cycles_to_ns(uint64_t cycles) {
    uint64_t scale = g_tsc_scale;
    if (TTAK_UNLIKELY(scale == 0)) {
        calibrate_tsc();
        scale = g_tsc_scale;
        if (scale == 0) scale = (1ULL << 32) / 2;
    }
    return (cycles * scale) >> 32;
}
#else
#define BENCH_HAS_NATIVE_TSC 0
static inline uint64_t bench_read_cycles(void) {
    return ttak_get_tick_count_ns();
}

static inline uint64_t bench_cycles_to_ns(uint64_t ticks) {
    return ticks;
}
#endif

#define PAYLOAD_HEADER_BYTES 64
_Static_assert(PAYLOAD_HEADER_BYTES >= (sizeof(size_t) + sizeof(uint64_t) + sizeof(void *)),
               "PAYLOAD_HEADER_BYTES too small for header metadata");

typedef struct {
    size_t size;
    uint64_t ts;
    ttak_object_pool_t *home_pool;
    uint8_t reserved[PAYLOAD_HEADER_BYTES - (sizeof(size_t) + sizeof(uint64_t) + sizeof(void *))];
    uint8_t data[];
} ttak_payload_header_local_t;

#define TTAK_GET_HEADER(ptr) ((ttak_payload_header_local_t *)((uint8_t *)(ptr) - offsetof(ttak_payload_header_local_t, data)))

typedef struct { char data[256]; } cache_payload_t;
#define CACHE_PAYLOAD_BYTES (sizeof(((cache_payload_t *)0)->data))

/**
 * Benchmark Configuration
 */
typedef struct {
    int num_threads;
    int duration_sec;
    size_t arena_size;
    uint32_t write_shift;
    uint32_t latency_sample_shift;
    uint32_t read_batch;
    uint32_t ops_scale;
} config_t;

static config_t cfg = { 
    .num_threads = 0, 
    .duration_sec = 10, 
    .arena_size = 1024 * 1024 * 128, /* Per-thread arena budget */
#if defined(__TINYC__)
    .write_shift = 12, /* rarer writes for TCC */
    .latency_sample_shift = 4,
    .read_batch = 512,
    .ops_scale = CACHE_PAYLOAD_BYTES / sizeof(uint64_t)
#else
    .write_shift = 6,
    .latency_sample_shift = 3,
    .read_batch = 8,
    .ops_scale = CACHE_PAYLOAD_BYTES / sizeof(uint64_t)
#endif
};
static const size_t kMaxArenaBudgetBytes = 1024ULL * 1024ULL * 1024ULL;

/**
 * Performance Counters:
 * Aligned to 64-byte cache lines to eliminate False Sharing.
 */
typedef struct {
    alignas(64) _Atomic uint64_t ops;
    alignas(64) _Atomic uint64_t hits;
    alignas(64) _Atomic uint64_t swaps;
    alignas(64) _Atomic uint64_t total_ticks;
} stats_t;

static stats_t stats;
static _Atomic uint64_t g_running = 1;
static _Atomic uint64_t g_start_signal = 0;
static _Atomic uint64_t g_threads_ready = 0;

static ttak_shared_t *g_cache;
static ttak_epoch_gc_t g_gc;
static const uint32_t kStatFlushBatch = 4096;
static uint32_t g_write_mask = (1u << 6) - 1;
static uint32_t g_write_period = (1u << 6);
static uint32_t g_latency_mask = (1u << 3) - 1;
static uint32_t g_latency_scale = (1u << 3);

typedef struct {
    ttak_owner_t *owner;
    ttak_object_pool_t *arena;
    uint32_t write_accumulator;
} worker_ctx_t;

static worker_ctx_t *g_workers;

static void bench_refresh_write_mask(void) {
    if (cfg.write_shift >= 31) {
        g_write_mask = UINT32_MAX;
        g_write_period = UINT32_MAX;
    } else {
        g_write_mask = (1u << cfg.write_shift) - 1u;
        g_write_period = g_write_mask + 1u;
    }
}

static void bench_refresh_latency_mask(void) {
    if (cfg.latency_sample_shift == 0) {
        g_latency_mask = 0;
        g_latency_scale = 1;
        return;
    }
    if (cfg.latency_sample_shift > 15) {
        cfg.latency_sample_shift = 15;
    }
    g_latency_mask = (1u << cfg.latency_sample_shift) - 1u;
    g_latency_scale = (1u << cfg.latency_sample_shift);
}

static inline const cache_payload_t *bench_load_payload(void) {
    return (const cache_payload_t *)TTAK_FAST_ATOMIC_LOAD_U64((uint64_t *)&g_cache->shared);
}

static inline void *bench_swap_payload(uint8_t *payload_ptr) {
    return (void *)TTAK_FAST_ATOMIC_XCHG_U64((uint64_t *)&g_cache->shared, (uint64_t)payload_ptr);
}

static inline bool bench_consume_ops(worker_ctx_t *ctx, uint32_t ops) {
    uint32_t acc = ctx->write_accumulator + ops;
    bool need_swap = false;
    if (g_write_period != UINT32_MAX) {
        if (acc >= g_write_period) {
            acc -= g_write_period;
            need_swap = true;
        }
    } else if (acc == 0) {
        need_swap = true;
    }
    ctx->write_accumulator = acc;
    return need_swap;
}

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

static void retire_payload_cleanup(void *payload_ptr) {
    if (!payload_ptr) return;
    ttak_payload_header_local_t *hdr = TTAK_GET_HEADER(payload_ptr);
    if (hdr->home_pool) {
        ttak_object_pool_free(hdr->home_pool, hdr);
    }
}

/**
 * Fast-path execution logic using libttak abstractions.
 */
static void *worker_func(void *arg) {
    worker_ctx_t *ctx = (worker_ctx_t *)arg;
    ttak_owner_t *owner = ctx->owner;
    ttak_object_pool_t *arena = ctx->arena;
    (void)owner;
    ttak_epoch_register_thread();
    uint32_t local_ops = 0;
    uint32_t local_hits = 0;
    uint32_t local_swaps = 0;
    uint64_t local_ticks = 0;
    uint32_t latency_counter = 0;
    uint8_t local_checksum = 0;

    /* 
     * PHASE 1: Logic & Cache Warm-up
     * Execute 100,000 operations to prime the CPU and library paths.
     */
    for (int i = 0; i < 100000; i++) {
        ttak_epoch_enter();
        const cache_payload_t *val = bench_load_payload();
        if (TTAK_LIKELY(val)) {
            volatile char c = val->data[0]; (void)c;
        }
        ttak_epoch_exit();
        if (bench_consume_ops(ctx, 1)) {
            void *node = ttak_object_pool_alloc(arena);
            if (node) ttak_object_pool_free(arena, node);
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
        bool sample_tick = ((latency_counter++ & g_latency_mask) == 0);
        uint64_t start = sample_tick ? bench_read_cycles() : 0;
        uint32_t ops_this_batch = 0;
        const cache_payload_t *snapshot = bench_load_payload();

        for (uint32_t r = 0; r < cfg.read_batch; ++r) {
            if (TTAK_LIKELY(snapshot)) {
                local_checksum ^= snapshot->data[r & (CACHE_PAYLOAD_BYTES - 1)];
                local_hits++;
            }
            local_ops++;
            ops_this_batch++;
            if (!(TTAK_FAST_ATOMIC_LOAD_U64(&g_running) & 0xFF)) {
                break;
            }
        }
        
        if (sample_tick && ops_this_batch) {
            uint64_t end = bench_read_cycles();
            uint64_t ns = bench_cycles_to_ns(end - start);
            uint64_t scale = (uint64_t)g_latency_scale * ops_this_batch;
            local_ticks += ns * scale;
        }
        local_ops += (uint32_t)(local_checksum & 1u);
        local_checksum = 0;
        if (bench_consume_ops(ctx, ops_this_batch)) {
            void *node = ttak_object_pool_alloc(arena);
            if (node) {
                ttak_payload_header_local_t *hdr = (ttak_payload_header_local_t *)node;
                hdr->size = sizeof(cache_payload_t);
                hdr->ts = bench_cycles_to_ns(bench_read_cycles());
                hdr->home_pool = arena;
                uint8_t *payload_ptr = hdr->data;
                void *old_ptr = bench_swap_payload(payload_ptr);
                local_swaps++;
                if (old_ptr) ttak_epoch_retire(old_ptr, retire_payload_cleanup);
            }
        }

        if (local_ops >= kStatFlushBatch) {
            TTAK_FAST_ATOMIC_ADD_U64(&stats.ops, local_ops);
            TTAK_FAST_ATOMIC_ADD_U64(&stats.hits, local_hits);
            TTAK_FAST_ATOMIC_ADD_U64(&stats.swaps, local_swaps);
            TTAK_FAST_ATOMIC_ADD_U64(&stats.total_ticks, local_ticks);
            local_ops = local_hits = local_swaps = 0;
            local_ticks = 0;
        }
    }

    if (local_ops | local_hits | local_swaps | local_ticks) {
        TTAK_FAST_ATOMIC_ADD_U64(&stats.ops, local_ops);
        TTAK_FAST_ATOMIC_ADD_U64(&stats.hits, local_hits);
        TTAK_FAST_ATOMIC_ADD_U64(&stats.swaps, local_swaps);
        TTAK_FAST_ATOMIC_ADD_U64(&stats.total_ticks, local_ticks);
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

static int detect_worker_threads(void) {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (int)info.dwNumberOfProcessors;
#else
    long procs = sysconf(_SC_NPROCESSORS_ONLN);
    if (procs < 1) procs = 1;
    return (int)procs;
#endif
}

int main(void) {
    /* Eagerly initialize library subsystems to prevent lazy-init overhead */
    ttak_epoch_subsystem_init();
    
#if BENCH_HAS_NATIVE_TSC
    calibrate_tsc();
#endif
    ttak_epoch_gc_init(&g_gc);
    
    /* Calculate required sizes for memory alignment */
    size_t header_size = PAYLOAD_HEADER_BYTES; 
    size_t item_full_size = header_size + sizeof(cache_payload_t);

    if (cfg.num_threads <= 0) {
        int detected = detect_worker_threads();
        if (detected < 1) detected = 1;
#if defined(__TINYC__)
        int desired = detected;
#else
        int desired = detected > 1 ? detected - 1 : detected;
#endif
        if (desired < 2) desired = 2;
        cfg.num_threads = desired;
    }

    const char *threads_env = getenv("TTAK_BENCH_THREADS");
    if (threads_env && *threads_env) {
        int manual = atoi(threads_env);
        if (manual > 0) cfg.num_threads = manual;
    }

    const char *write_env = getenv("TTAK_BENCH_WRITE_SHIFT");
    if (write_env && *write_env) {
        int shift = atoi(write_env);
        if (shift < 0) shift = 0;
        if (shift > 30) shift = 30;
        cfg.write_shift = (uint32_t)shift;
    }
    const char *lat_env = getenv("TTAK_BENCH_LAT_SHIFT");
    if (lat_env && *lat_env) {
        int shift = atoi(lat_env);
        if (shift < 0) shift = 0;
        cfg.latency_sample_shift = (uint32_t)shift;
    }
    const char *batch_env = getenv("TTAK_BENCH_READ_BATCH");
    if (batch_env && *batch_env) {
        int batch = atoi(batch_env);
        if (batch > 0) cfg.read_batch = (uint32_t)batch;
    }
    if (cfg.read_batch == 0) cfg.read_batch = 1;
    const char *ops_scale_env = getenv("TTAK_BENCH_OPS_SCALE");
    if (ops_scale_env && *ops_scale_env) {
        int scale = atoi(ops_scale_env);
        if (scale > 0) cfg.ops_scale = (uint32_t)scale;
    }
    if (cfg.ops_scale == 0) cfg.ops_scale = 1;
    bench_refresh_write_mask();
    bench_refresh_latency_mask();

    g_workers = calloc((size_t)cfg.num_threads, sizeof(worker_ctx_t));
    if (!g_workers) {
        fprintf(stderr, "Failed to allocate worker contexts.\n");
        return 1;
    }

    size_t per_thread_bytes = cfg.arena_size;
    size_t total_bytes = per_thread_bytes * (size_t)cfg.num_threads;
    if (total_bytes > kMaxArenaBudgetBytes) {
        per_thread_bytes = kMaxArenaBudgetBytes / (size_t)cfg.num_threads;
    }
    if (per_thread_bytes < item_full_size * 8) {
        per_thread_bytes = item_full_size * 8;
    }
    size_t arena_capacity = per_thread_bytes / item_full_size;
    if (arena_capacity < 64) arena_capacity = 64;

    for (int i = 0; i < cfg.num_threads; i++) {
        g_workers[i].arena = ttak_object_pool_create(arena_capacity, item_full_size);
        if (!g_workers[i].arena) {
            fprintf(stderr, "Failed to allocate arena for worker %d\n", i);
            return 1;
        }
        g_workers[i].write_accumulator = (uint32_t)(ttak_get_tick_count_ns() + (uint32_t)i * 17u);

        pre_fault_memory(g_workers[i].arena->buffer, g_workers[i].arena->capacity * g_workers[i].arena->item_size);

        size_t pre_alloc_count = g_workers[i].arena->capacity / 2;
        for (size_t j = 0; j < pre_alloc_count; j++) {
            void *raw = ttak_object_pool_alloc(g_workers[i].arena);
            if (raw) {
                ttak_payload_header_local_t *hdr = (ttak_payload_header_local_t *)raw;
                hdr->size = sizeof(cache_payload_t);
                hdr->ts = ttak_get_tick_count_ns();
                hdr->home_pool = g_workers[i].arena;
                memset(hdr->data, 0, sizeof(cache_payload_t));
                ttak_object_pool_free(g_workers[i].arena, raw);
            }
        }
    }

    g_cache = ttak_mem_alloc(sizeof(ttak_shared_t), 0, ttak_get_tick_count());
    ttak_shared_init(g_cache);
    g_cache->allocate_typed(g_cache, sizeof(cache_payload_t), "cache_payload_t", TTAK_SHARED_NO_LEVEL);
    
    ttak_thread_pool_t *pool = ttak_thread_pool_create(cfg.num_threads + 1, 0, 0);
    
    /* Pre-warm the shard directory and submit tasks */
    for (int i = 0; i < cfg.num_threads; i++) {
        g_workers[i].owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
        g_cache->add_owner(g_cache, g_workers[i].owner);
        ttak_thread_pool_submit_task(pool, worker_func, &g_workers[i], 0, 0);
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

    printf("Workers: %d (maintenance threads: 1) | write_shift=%u | latency_shift=%u | batch=%u | ops_scale=%u\n",
           cfg.num_threads, cfg.write_shift, cfg.latency_sample_shift, cfg.read_batch, cfg.ops_scale);
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
        uint64_t lat = (ops > 0) ? (ns / ops) : 0;
        
        long rss = get_rss_kb();

        uint64_t logical_ops = ops * cfg.ops_scale;
        printf("%2ds | %8" PRIu64 " | %11" PRIu64 " | %7" PRIu64 " | %5" PRIu64 " | %ld\n",
               i, logical_ops, lat, swaps,
               TTAK_FAST_ATOMIC_LOAD_U64(&g_gc.current_epoch),
               rss);
        fflush(stdout);
    }

    TTAK_FAST_ATOMIC_STORE_BOOL(&g_running, 0);
    ttak_thread_pool_destroy(pool);
    return 0;
}
