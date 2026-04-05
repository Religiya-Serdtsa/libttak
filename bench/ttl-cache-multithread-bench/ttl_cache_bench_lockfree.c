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

typedef struct {
    uint64_t key;
    uint64_t expire_ns;
    cache_payload_t value;
} cache_item_data_t;

typedef ttak_payload_header_local_t cache_entry_t;

typedef struct {
    cache_item_data_t * _Atomic ptr;
} cache_bucket_t;

typedef struct {
    size_t bucket_count;
    size_t bucket_mask;
    cache_bucket_t buckets[];
} cache_table_t;

typedef enum {
    CACHE_RESULT_MISS = 0,
    CACHE_RESULT_HIT,
    CACHE_RESULT_EXPIRED
} cache_lookup_kind_t;

typedef struct {
    cache_lookup_kind_t kind;
    cache_entry_t *entry;
    cache_item_data_t *item;
} cache_lookup_result_t;

typedef struct {
    cache_entry_t *replaced;
    bool inserted;
    bool forced;
} cache_store_result_t;

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
    uint32_t write_pct;
    uint64_t ttl_ns;
    uint64_t key_space;
    uint64_t hot_key_space;
    uint32_t hot_key_pct;
    size_t table_size;
    uint32_t max_probe;
    uint32_t maintenance_scan;
    uint32_t warmup_ops;
} config_t;

static config_t cfg = { 
    .num_threads = 0, 
    .duration_sec = 300, 
    .arena_size = 1024 * 1024 * 128, /* Per-thread arena budget */
#if defined(__TINYC__)
    .write_shift = 12, /* rarer writes for TCC */
    .latency_sample_shift = 4,
    .read_batch = 512,
    .ops_scale = 1
#else
    .write_shift = 6,
    .latency_sample_shift = 3,
    .read_batch = 8,
    .ops_scale = 1
#endif
    ,
    .write_pct = 5,
    .ttl_ns = 5 * 1000 * 1000ULL,
    .key_space = 1u << 20,
    .hot_key_space = 1u << 16,
    .hot_key_pct = 90,
    .table_size = 1u << 20,
    .max_probe = 8,
    .maintenance_scan = 4096,
    .warmup_ops = 100000
};
static const size_t kMaxArenaBudgetBytes = 1024ULL * 1024ULL * 1024ULL;

/**
 * Performance Counters:
 * Aligned to 64-byte cache lines to eliminate False Sharing.
 */
typedef struct {
    alignas(64) _Atomic uint64_t read_ops;
    alignas(64) _Atomic uint64_t write_ops;
    alignas(64) _Atomic uint64_t hits;
    alignas(64) _Atomic uint64_t misses;
    alignas(64) _Atomic uint64_t expired;
    alignas(64) _Atomic uint64_t evictions;
    alignas(64) _Atomic uint64_t cleanups;
    alignas(64) _Atomic uint64_t retirements;
    alignas(64) _Atomic uint64_t total_ticks;
} stats_t;

static stats_t stats;
static _Atomic uint64_t g_running = 1;
static _Atomic uint64_t g_start_signal = 0;
static _Atomic uint64_t g_threads_ready = 0;
static _Atomic uint64_t g_abort_signal = 0;

static ttak_shared_t *g_cache;
static cache_table_t *g_table;
static ttak_epoch_gc_t g_gc;
static const uint32_t kStatFlushBatch = 4096;
static uint32_t g_write_mask = 5;
static uint32_t g_write_period = 100;
static uint32_t g_latency_mask = (1u << 3) - 1;
static uint32_t g_latency_scale = (1u << 3);
static _Atomic size_t g_maintenance_cursor = 0;

typedef struct {
    ttak_owner_t *owner;
    ttak_object_pool_t *arena;
    uint32_t write_accumulator;
    uint64_t rng_state;
    cache_table_t *table;
} worker_ctx_t;

static worker_ctx_t *g_workers;

static void bench_refresh_write_mask(void) {
    if (cfg.write_pct == 0) {
        if (cfg.write_shift >= 31) {
            cfg.write_pct = 0;
        } else {
            uint32_t denom = (1u << cfg.write_shift);
            if (denom == 0) denom = 1;
            cfg.write_pct = denom >= 100 ? 1u : (100u / denom);
            if (cfg.write_pct == 0) cfg.write_pct = 1;
        }
    }
    if (cfg.write_pct > 100) cfg.write_pct = 100;
    g_write_mask = cfg.write_pct;
    if (cfg.write_pct == 0) {
        g_write_period = UINT32_MAX;
    } else {
        g_write_period = 100;
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

static inline bool bench_consume_ops(worker_ctx_t *ctx, uint32_t ops) {
    if (g_write_period == UINT32_MAX || g_write_mask == 0) {
        ctx->write_accumulator = 0;
        return false;
    }
    uint32_t acc = ctx->write_accumulator;
    uint64_t scaled = (uint64_t)ops * (uint64_t)g_write_mask;
    uint64_t sum = (uint64_t)acc + scaled;
    bool trigger = false;
    while (sum >= g_write_period) {
        sum -= g_write_period;
        trigger = true;
    }
    ctx->write_accumulator = (uint32_t)sum;
    return trigger;
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

static inline cache_item_data_t *cache_payload_from_ptr(void *ptr) {
    return (cache_item_data_t *)ptr;
}

static inline cache_entry_t *cache_entry_from_payload(cache_item_data_t *payload) {
    return payload ? TTAK_GET_HEADER(payload) : NULL;
}

static inline uint64_t bench_now_ns(void) {
    return ttak_get_tick_count_ns();
}

static inline uint64_t bench_mix_u64(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static cache_table_t *cache_table_create(size_t desired_buckets) {
    size_t buckets = 1;
    while (buckets < desired_buckets) {
        buckets <<= 1;
        if (buckets == 0) {
            buckets = 1ull << 20;
            break;
        }
    }
    size_t bytes = sizeof(cache_table_t) + buckets * sizeof(cache_bucket_t);
    cache_table_t *table = ttak_mem_alloc(bytes, 0, ttak_get_tick_count());
    if (!table) return NULL;
    memset(table, 0, bytes);
    table->bucket_count = buckets;
    table->bucket_mask = buckets - 1;
    return table;
}

static inline cache_table_t *bench_cache_table(void) {
    return g_table;
}

static inline void bench_install_table(cache_table_t *table) {
    g_table = table;
    if (g_cache) {
        g_cache->shared = table;
    }
}

static inline cache_lookup_result_t cache_table_lookup(cache_table_t *table, uint64_t key, uint64_t now_ns) {
    cache_lookup_result_t result = { .kind = CACHE_RESULT_MISS, .entry = NULL, .item = NULL };
    if (!table) return result;
    size_t mask = table->bucket_mask;
    size_t idx = bench_mix_u64(key) & mask;
    uint32_t max_probe = cfg.max_probe ? cfg.max_probe : 1;
    for (uint32_t probe = 0; probe < max_probe; ++probe) {
        cache_item_data_t *payload = atomic_load_explicit(&table->buckets[idx].ptr, memory_order_acquire);
        if (!payload) {
            result.kind = CACHE_RESULT_MISS;
            return result;
        }
        if (payload->key == key) {
            if (payload->expire_ns > now_ns) {
                result.kind = CACHE_RESULT_HIT;
                result.entry = cache_entry_from_payload(payload);
                result.item = payload;
                return result;
            }
            /* Try to clear expired bucket atomically */
            cache_item_data_t *expected = payload;
            if (atomic_compare_exchange_strong_explicit(&table->buckets[idx].ptr, &expected, NULL,
                                                       memory_order_release, memory_order_relaxed)) {
                ttak_epoch_retire(payload, retire_payload_cleanup);
                TTAK_FAST_ATOMIC_ADD_U64(&stats.retirements, 1);
                result.kind = CACHE_RESULT_EXPIRED;
            } else {
                /* Someone else replaced/cleared it, retry or treat as miss/expired */
                result.kind = CACHE_RESULT_MISS;
            }
            return result;
        }
        idx = (idx + 1) & mask;
    }
    return result;
}

static cache_store_result_t cache_table_store(cache_table_t *table, cache_item_data_t *payload, uint64_t now_ns) {
    cache_store_result_t result = {0};
    if (!table || !payload) return result;
    uint64_t key = payload->key;
    size_t mask = table->bucket_mask;
    size_t idx = bench_mix_u64(key) & mask;
    uint32_t max_probe = cfg.max_probe ? cfg.max_probe : 1;
    
    for (uint32_t probe = 0; probe < max_probe; ++probe) {
        cache_item_data_t *existing = atomic_load_explicit(&table->buckets[idx].ptr, memory_order_acquire);
        if (!existing) {
            cache_item_data_t *expected = NULL;
            if (atomic_compare_exchange_strong_explicit(&table->buckets[idx].ptr, &expected, payload,
                                                       memory_order_release, memory_order_relaxed)) {
                result.inserted = true;
                return result;
            }
            /* CAS failed, something else was inserted, retry this bucket */
            probe--; 
            continue;
        }
        if (existing->key == key || existing->expire_ns <= now_ns) {
            if (atomic_compare_exchange_strong_explicit(&table->buckets[idx].ptr, &existing, payload,
                                                       memory_order_release, memory_order_relaxed)) {
                result.inserted = true;
                result.replaced = cache_entry_from_payload(existing);
                result.forced = (existing->key != key);
                return result;
            }
            /* CAS failed, retry this bucket */
            probe--;
            continue;
        }
        idx = (idx + 1) & mask;
    }

    /* Forced eviction at the end of probe sequence */
    cache_item_data_t *existing = atomic_exchange_explicit(&table->buckets[idx].ptr, payload, memory_order_acq_rel);
    if (existing) {
        result.replaced = cache_entry_from_payload(existing);
        result.forced = true;
    }
    result.inserted = true;
    return result;
}

static size_t cache_table_cleanup(cache_table_t *table, size_t cursor, size_t budget, uint64_t now_ns) {
    if (!table || table->bucket_count == 0) return cursor;
    size_t mask = table->bucket_mask;
    size_t start = cursor;
    size_t count = table->bucket_count;
    for (size_t i = 0; i < budget; ++i) {
        cache_item_data_t *payload = atomic_load_explicit(&table->buckets[start].ptr, memory_order_acquire);
        if (payload && payload->expire_ns <= now_ns) {
            cache_item_data_t *expected = payload;
            if (atomic_compare_exchange_strong_explicit(&table->buckets[start].ptr, &expected, NULL,
                                                       memory_order_release, memory_order_relaxed)) {
                ttak_epoch_retire(payload, retire_payload_cleanup);
                TTAK_FAST_ATOMIC_ADD_U64(&stats.cleanups, 1);
                TTAK_FAST_ATOMIC_ADD_U64(&stats.retirements, 1);
            }
        }
        start = (start + 1) & mask;
    }
    return start % count;
}

static inline uint64_t bench_xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x ? x : 0x123456789abcdefULL;
    return *state;
}

static inline uint64_t bench_select_key(worker_ctx_t *ctx) {
    uint64_t hot_limit = cfg.hot_key_space > 0 ? cfg.hot_key_space : 1;
    uint64_t universe = cfg.key_space > 0 ? cfg.key_space : hot_limit;
    if (hot_limit > universe) hot_limit = universe;
    uint32_t pct = cfg.hot_key_pct > 100 ? 100 : cfg.hot_key_pct;
    uint64_t r = bench_xorshift64(&ctx->rng_state);
    bool choose_hot = (pct == 0) ? false : ((r % 100u) < pct);
    uint64_t base = choose_hot ? 0 : hot_limit;
    uint64_t span = choose_hot ? hot_limit : (universe - hot_limit);
    if (span == 0) span = hot_limit ? hot_limit : 1;
    uint64_t key = base + (bench_xorshift64(&ctx->rng_state) % span);
    return key;
}

static inline void bench_fill_payload(cache_item_data_t *item, uint64_t key, uint64_t stamp) {
    uint64_t mix = bench_mix_u64(key ^ stamp);
    size_t words = sizeof(item->value.data) / sizeof(uint64_t);
    if (words == 0) {
        memset(item->value.data, (int)mix, sizeof(item->value.data));
        return;
    }
    uint64_t *dst = (uint64_t *)item->value.data;
    for (size_t i = 0; i < words; ++i) {
        dst[i] = mix + (uint64_t)i;
    }
}

static bool bench_perform_write(worker_ctx_t *ctx, cache_table_t *table, uint64_t key,
                                uint64_t now_ns, uint64_t *local_evictions, uint64_t *local_retirements) {
    cache_entry_t *hdr = ttak_object_pool_alloc(ctx->arena);
    if (!hdr) return false;
    hdr->size = sizeof(cache_item_data_t);
    hdr->ts = now_ns;
    hdr->home_pool = ctx->arena;
    cache_item_data_t *item = (cache_item_data_t *)hdr->data;
    item->key = key;
    item->expire_ns = now_ns + cfg.ttl_ns;
    bench_fill_payload(item, key, now_ns);
    cache_store_result_t store_res = cache_table_store(table, item, now_ns);
    if (!store_res.inserted) {
        ttak_object_pool_free(ctx->arena, hdr);
        return false;
    }
    if (store_res.replaced) {
        (*local_retirements)++;
        ttak_epoch_retire(store_res.replaced->data, retire_payload_cleanup);
    }
    if (store_res.forced) {
        (*local_evictions)++;
    }
    return true;
}

/**
 * Fast-path execution logic using libttak abstractions.
 */
static void *worker_func(void *arg) {
    worker_ctx_t *ctx = (worker_ctx_t *)arg;
    ttak_owner_t *owner = ctx->owner;
    cache_table_t *table = ctx->table ? ctx->table : bench_cache_table();
    (void)owner;
    ttak_epoch_register_thread();
    uint64_t local_reads = 0;
    uint64_t local_writes = 0;
    uint64_t local_hits = 0;
    uint64_t local_misses = 0;
    uint64_t local_expired = 0;
    uint64_t local_evictions = 0;
    uint64_t local_retired = 0;
    uint64_t local_ticks = 0;
    uint32_t latency_counter = 0;
    uint8_t local_checksum = 0;

    uint32_t warmup_ops = cfg.warmup_ops ? cfg.warmup_ops : 100000;
    bool aborting = false;
    for (uint32_t i = 0; i < warmup_ops; ++i) {
        if (TTAK_FAST_ATOMIC_LOAD_U64(&g_abort_signal)) {
            aborting = true;
            break;
        }
        uint64_t now_ns = bench_now_ns();
        uint64_t key = bench_select_key(ctx);
        ttak_epoch_enter();
        cache_lookup_result_t lookup = cache_table_lookup(table, key, now_ns);
        if (lookup.kind == CACHE_RESULT_HIT && lookup.item) {
            volatile uint8_t *payload_bytes = (volatile uint8_t *)lookup.item->value.data;
            local_checksum ^= payload_bytes[i & (CACHE_PAYLOAD_BYTES - 1)];
        }
        ttak_epoch_exit();
        if (bench_consume_ops(ctx, 1)) {
            uint64_t write_stamp = bench_now_ns();
            bench_perform_write(ctx, table, bench_select_key(ctx), write_stamp, &local_evictions, &local_retired);
        }
    }
    if (aborting) {
        goto worker_cleanup;
    }
    local_reads = local_writes = local_hits = local_misses = local_expired = 0;
    local_evictions = local_retired = 0;
    local_ticks = 0;
    local_checksum = 0;

    TTAK_FAST_ATOMIC_ADD_U64(&g_threads_ready, 1);
    while (TTAK_FAST_ATOMIC_LOAD_U64(&g_start_signal) == 0) {
        if (TTAK_FAST_ATOMIC_LOAD_U64(&g_abort_signal)) {
            goto worker_cleanup;
        }
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile ("pause");
#endif
    }

    while ((TTAK_FAST_ATOMIC_LOAD_U64(&g_running) & 0xFF) && !TTAK_FAST_ATOMIC_LOAD_U64(&g_abort_signal)) {
        bool sample_tick = ((latency_counter++ & g_latency_mask) == 0);
        uint64_t start = sample_tick ? bench_read_cycles() : 0;
        uint32_t ops_this_batch = 0;

        ttak_epoch_enter();
        for (uint32_t r = 0; r < cfg.read_batch; ++r) {
            if (!(TTAK_FAST_ATOMIC_LOAD_U64(&g_running) & 0xFF)) {
                break;
            }
            uint64_t now_ns = bench_now_ns();
            uint64_t key = bench_select_key(ctx);
            cache_lookup_result_t lookup = cache_table_lookup(table, key, now_ns);
            if (lookup.kind == CACHE_RESULT_HIT && lookup.item) {
                volatile uint8_t *payload_bytes = (volatile uint8_t *)lookup.item->value.data;
                local_checksum ^= payload_bytes[r & (CACHE_PAYLOAD_BYTES - 1)];
                local_hits++;
            } else if (lookup.kind == CACHE_RESULT_EXPIRED) {
                local_expired++;
            } else {
                local_misses++;
            }
            local_reads++;
            ops_this_batch++;

            if (bench_consume_ops(ctx, 1)) {
                uint64_t write_key = (ctx->rng_state & 1u) ? key : bench_select_key(ctx);
                uint64_t write_stamp = bench_now_ns();
                if (bench_perform_write(ctx, table, write_key, write_stamp, &local_evictions, &local_retired)) {
                    local_writes++;
                    ops_this_batch++;
                }
            }
        }
        ttak_epoch_exit();

        if (sample_tick && ops_this_batch) {
            uint64_t end = bench_read_cycles();
            uint64_t ns = bench_cycles_to_ns(end - start);
            uint64_t scale = (uint64_t)g_latency_scale * ops_this_batch;
            local_ticks += ns * scale;
        }

        local_checksum = 0;
        if ((local_reads + local_writes) >= kStatFlushBatch) {
            TTAK_FAST_ATOMIC_ADD_U64(&stats.read_ops, local_reads);
            TTAK_FAST_ATOMIC_ADD_U64(&stats.write_ops, local_writes);
            TTAK_FAST_ATOMIC_ADD_U64(&stats.hits, local_hits);
            TTAK_FAST_ATOMIC_ADD_U64(&stats.misses, local_misses);
            TTAK_FAST_ATOMIC_ADD_U64(&stats.expired, local_expired);
            TTAK_FAST_ATOMIC_ADD_U64(&stats.evictions, local_evictions);
            TTAK_FAST_ATOMIC_ADD_U64(&stats.retirements, local_retired);
            TTAK_FAST_ATOMIC_ADD_U64(&stats.total_ticks, local_ticks);
            local_reads = local_writes = 0;
            local_hits = local_misses = local_expired = 0;
            local_evictions = local_retired = 0;
            local_ticks = 0;
        }
    }

worker_cleanup:
    if (local_reads || local_writes || local_hits || local_misses || local_expired || local_evictions || local_retired || local_ticks) {
        TTAK_FAST_ATOMIC_ADD_U64(&stats.read_ops, local_reads);
        TTAK_FAST_ATOMIC_ADD_U64(&stats.write_ops, local_writes);
        TTAK_FAST_ATOMIC_ADD_U64(&stats.hits, local_hits);
        TTAK_FAST_ATOMIC_ADD_U64(&stats.misses, local_misses);
        TTAK_FAST_ATOMIC_ADD_U64(&stats.expired, local_expired);
        TTAK_FAST_ATOMIC_ADD_U64(&stats.evictions, local_evictions);
        TTAK_FAST_ATOMIC_ADD_U64(&stats.retirements, local_retired);
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
    ttak_epoch_register_thread();
    while (TTAK_FAST_ATOMIC_LOAD_U64(&g_start_signal) == 0) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile ("pause");
#endif
    }
    while (TTAK_FAST_ATOMIC_LOAD_U64(&g_running) & 0xFF) {
        uint64_t now_ns = bench_now_ns();
        size_t budget = cfg.maintenance_scan ? cfg.maintenance_scan : 1024;
        size_t cursor = atomic_load_explicit(&g_maintenance_cursor, memory_order_relaxed);
        if (g_table && budget > 0) {
            ttak_epoch_enter();
            size_t next = cache_table_cleanup(g_table, cursor % g_table->bucket_count, budget, now_ns);
            ttak_epoch_exit();
            atomic_store_explicit(&g_maintenance_cursor, next, memory_order_relaxed);
        }
        bench_usleep_us(100000); 
        ttak_epoch_reclaim();
        ttak_epoch_gc_rotate(&g_gc);
    }
    ttak_epoch_deregister_thread();
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
    int exit_code = 0;
    /* Eagerly initialize library subsystems to prevent lazy-init overhead */
    ttak_epoch_subsystem_init();
    
#if BENCH_HAS_NATIVE_TSC
    calibrate_tsc();
#endif
    ttak_epoch_gc_init(&g_gc);
    
    /* Calculate required sizes for memory alignment */
    size_t header_size = PAYLOAD_HEADER_BYTES; 
    size_t item_full_size = header_size + sizeof(cache_item_data_t);

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
    const char *write_pct_env = getenv("TTAK_BENCH_WRITE_PCT");
    if (write_pct_env && *write_pct_env) {
        int pct = atoi(write_pct_env);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        cfg.write_pct = (uint32_t)pct;
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
    const char *ttl_ns_env = getenv("TTAK_BENCH_TTL_NS");
    if (ttl_ns_env && *ttl_ns_env) {
        long long ns = atoll(ttl_ns_env);
        if (ns >= 0) cfg.ttl_ns = (uint64_t)ns;
    } else {
        const char *ttl_ms_env = getenv("TTAK_BENCH_TTL_MS");
        if (ttl_ms_env && *ttl_ms_env) {
            long long ms = atoll(ttl_ms_env);
            if (ms >= 0) cfg.ttl_ns = (uint64_t)ms * 1000000ULL;
        }
        const char *ttl_us_env = getenv("TTAK_BENCH_TTL_US");
        if (ttl_us_env && *ttl_us_env) {
            long long us = atoll(ttl_us_env);
            if (us >= 0) cfg.ttl_ns = (uint64_t)us * 1000ULL;
        }
    }
    const char *key_env = getenv("TTAK_BENCH_KEYSPACE");
    if (key_env && *key_env) {
        long long ks = atoll(key_env);
        if (ks > 0) cfg.key_space = (uint64_t)ks;
    }
    const char *hot_env = getenv("TTAK_BENCH_HOT_KEYS");
    if (hot_env && *hot_env) {
        long long hk = atoll(hot_env);
        if (hk > 0) cfg.hot_key_space = (uint64_t)hk;
    }
    const char *hot_pct_env = getenv("TTAK_BENCH_HOT_PCT");
    if (hot_pct_env && *hot_pct_env) {
        int pct = atoi(hot_pct_env);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        cfg.hot_key_pct = (uint32_t)pct;
    }
    const char *table_env = getenv("TTAK_BENCH_TABLE_SIZE");
    if (table_env && *table_env) {
        long long ts = atoll(table_env);
        if (ts > 0) cfg.table_size = (size_t)ts;
    }
    const char *probe_env = getenv("TTAK_BENCH_MAX_PROBE");
    if (probe_env && *probe_env) {
        int pv = atoi(probe_env);
        if (pv > 0) cfg.max_probe = (uint32_t)pv;
    }
    const char *scan_env = getenv("TTAK_BENCH_MAINT_SCAN");
    if (scan_env && *scan_env) {
        int sc = atoi(scan_env);
        if (sc > 0) cfg.maintenance_scan = (uint32_t)sc;
    }
    const char *warm_env = getenv("TTAK_BENCH_WARMUP");
    if (warm_env && *warm_env) {
        int warm = atoi(warm_env);
        if (warm > 0) cfg.warmup_ops = (uint32_t)warm;
    }
    const char *duration_env = getenv("TTAK_BENCH_DURATION_SEC");
    if (duration_env && *duration_env) {
        int duration = atoi(duration_env);
        if (duration > 0) cfg.duration_sec = duration;
    }
    if (cfg.ttl_ns == 0) cfg.ttl_ns = 5 * 1000 * 1000ULL;
    if (cfg.key_space == 0) cfg.key_space = 1u << 18;
    if (cfg.hot_key_space == 0 || cfg.hot_key_space > cfg.key_space) {
        cfg.hot_key_space = cfg.key_space / 8;
        if (cfg.hot_key_space == 0) cfg.hot_key_space = 1;
    }
    if (cfg.table_size < cfg.hot_key_space * 2) cfg.table_size = cfg.hot_key_space * 2;
    if (cfg.table_size < 1024) cfg.table_size = 1024;
    if (cfg.max_probe == 0) cfg.max_probe = 8;
    if (cfg.maintenance_scan == 0) cfg.maintenance_scan = 4096;
    if (cfg.hot_key_pct > 100) cfg.hot_key_pct = 100;
    if (cfg.warmup_ops == 0) cfg.warmup_ops = 100000;
    if (cfg.write_pct > 100) cfg.write_pct = 100;
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
                hdr->size = sizeof(cache_item_data_t);
                hdr->ts = ttak_get_tick_count_ns();
                hdr->home_pool = g_workers[i].arena;
                cache_item_data_t *item = (cache_item_data_t *)hdr->data;
                item->key = 0;
                item->expire_ns = 0;
                memset(item->value.data, 0, sizeof(item->value.data));
                ttak_object_pool_free(g_workers[i].arena, raw);
            }
        }
        uint64_t seed = bench_mix_u64(ttak_get_tick_count_ns() ^ ((uint64_t)i + 1) * 0x9e3779b97f4a7c15ULL);
        if (seed == 0) seed = ((uint64_t)i + 1) * 17ULL;
        g_workers[i].rng_state = seed;
        g_workers[i].table = NULL;
    }

    cache_table_t *table = cache_table_create(cfg.table_size);
    if (!table) {
        fprintf(stderr, "Failed to allocate cache table\n");
        return 1;
    }
    size_t table_bytes = sizeof(cache_table_t) + table->bucket_count * sizeof(cache_bucket_t);
    pre_fault_memory(table, table_bytes);
    g_table = table;

    g_cache = ttak_mem_alloc(sizeof(ttak_shared_t), 0, ttak_get_tick_count());
    ttak_shared_init(g_cache);
    g_cache->allocate_typed(g_cache, sizeof(cache_item_data_t), "cache_item_data_t", TTAK_SHARED_NO_LEVEL);
    bench_install_table(g_table);
    for (int i = 0; i < cfg.num_threads; ++i) {
        g_workers[i].table = g_table;
    }
    
    ttak_thread_pool_t *pool = ttak_thread_pool_create(cfg.num_threads + 1, 0, 0);
    if (!pool) {
        fprintf(stderr, "Failed to create thread pool (workers=%d)\n", cfg.num_threads + 1);
        return 1;
    }

    /* Pre-warm the shard directory and submit tasks */
    for (int i = 0; i < cfg.num_threads; i++) {
        g_workers[i].owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
        g_cache->add_owner(g_cache, g_workers[i].owner);
        if (!ttak_thread_pool_submit_task(pool, worker_func, &g_workers[i], 0, 0)) {
            fprintf(stderr, "Failed to schedule worker %d\n", i);
            exit_code = 1;
            goto abort_workers;
        }
    }
    if (!ttak_thread_pool_submit_task(pool, maintenance_task, NULL, 0, 0)) {
        fprintf(stderr, "Failed to schedule maintenance task\n");
        exit_code = 1;
        goto abort_workers;
    }

    /* 
     * WAIT FOR ALL WORKERS TO BE FULLY WARMED UP 
     * This ensures t=1s starts at full velocity.
     */
    printf("Warming up %d worker threads...\n", cfg.num_threads);
    const uint64_t warmup_timeout_ns = 60ULL * 1000ULL * 1000ULL * 1000ULL;
    uint64_t warmup_start_ns = bench_now_ns();
    uint64_t last_report_ns = warmup_start_ns;
    while (true) {
        uint64_t ready = TTAK_FAST_ATOMIC_LOAD_U64(&g_threads_ready);
        if (ready >= (uint64_t)cfg.num_threads) {
            break;
        }
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile ("pause");
#endif
        bench_usleep_us(1000);
        uint64_t now_ns = bench_now_ns();
        if (now_ns - last_report_ns >= 1000000000ULL) {
            printf("  warmup: %" PRIu64 "/%d ready\n", ready, cfg.num_threads);
            fflush(stdout);
            last_report_ns = now_ns;
        }
        if (now_ns - warmup_start_ns > warmup_timeout_ns) {
            fprintf(stderr, "ERROR: worker warmup timed out (%" PRIu64 "/%d ready)\n", ready, cfg.num_threads);
            exit_code = 1;
            goto abort_workers;
        }
    }

    printf("Workers: %d (maintenance threads: 1) | write_pct=%u | ttl_ns=%" PRIu64 " | batch=%u | keyspace=%" PRIu64 " hot=%" PRIu64 " (%u%%)\n",
           cfg.num_threads, cfg.write_pct, cfg.ttl_ns, cfg.read_batch, cfg.key_space,
           cfg.hot_key_space, cfg.hot_key_pct);
    printf("Time | Ops/s | Hit%% | Miss%% | Exp%% | Writes/s | Latency(ns) | Epoch | RSS(KB) | Evict/s | Clean/s | Retire/s\n");
    printf("------------------------------------------------------------------------------------------------------------------\n");

    /* Release all threads simultaneously */
    TTAK_FAST_ATOMIC_STORE_BOOL(&g_start_signal, 1);

    for (int i = 1; i <= cfg.duration_sec; i++) {
        bench_sleep_s(1);
        uint64_t reads = TTAK_FAST_ATOMIC_XCHG_U64(&stats.read_ops, 0);
        uint64_t writes = TTAK_FAST_ATOMIC_XCHG_U64(&stats.write_ops, 0);
        uint64_t hits = TTAK_FAST_ATOMIC_XCHG_U64(&stats.hits, 0);
        uint64_t misses = TTAK_FAST_ATOMIC_XCHG_U64(&stats.misses, 0);
        uint64_t expired = TTAK_FAST_ATOMIC_XCHG_U64(&stats.expired, 0);
        uint64_t evictions = TTAK_FAST_ATOMIC_XCHG_U64(&stats.evictions, 0);
        uint64_t cleanups = TTAK_FAST_ATOMIC_XCHG_U64(&stats.cleanups, 0);
        uint64_t retired = TTAK_FAST_ATOMIC_XCHG_U64(&stats.retirements, 0);
        uint64_t ticks = TTAK_FAST_ATOMIC_XCHG_U64(&stats.total_ticks, 0);
        uint64_t ops = reads + writes;
        uint64_t lat = (ops > 0) ? (ticks / ops) : 0;
        double hit_pct = (reads > 0) ? ((double)hits * 100.0 / (double)reads) : 0.0;
        double miss_pct = (reads > 0) ? ((double)misses * 100.0 / (double)reads) : 0.0;
        double exp_pct = (reads > 0) ? ((double)expired * 100.0 / (double)reads) : 0.0;
        
        long rss = get_rss_kb();

        printf("%2ds | %8" PRIu64 " | %6.2f | %6.2f | %6.2f | %8" PRIu64 " | %11" PRIu64 " | %5" PRIu64 " | %7ld | %7" PRIu64 " | %7" PRIu64 " | %9" PRIu64 "\n",
               i, ops, hit_pct, miss_pct, exp_pct, writes, lat,
               TTAK_FAST_ATOMIC_LOAD_U64(&g_gc.current_epoch),
               rss, evictions, cleanups, retired);
        fflush(stdout);
    }

    TTAK_FAST_ATOMIC_STORE_BOOL(&g_running, 0);
    goto cleanup;

abort_workers:
    TTAK_FAST_ATOMIC_STORE_BOOL(&g_abort_signal, 1);
    TTAK_FAST_ATOMIC_STORE_BOOL(&g_running, 0);
    TTAK_FAST_ATOMIC_STORE_BOOL(&g_start_signal, 1);

cleanup:
    if (pool) {
        ttak_thread_pool_destroy(pool);
    }
    return exit_code;
}
