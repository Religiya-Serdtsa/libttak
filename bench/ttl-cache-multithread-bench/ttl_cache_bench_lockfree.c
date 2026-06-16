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
#  define bench_sleep_s(s)   Sleep((DWORD)((s) * 1000u))
#  define bench_usleep_us(us) Sleep((DWORD)(((us) + 999) / 1000))
static long get_rss_kb(void) {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (long)(pmc.WorkingSetSize / 1024);
    return 0;
}
#else
#  include <unistd.h>
#  include <pthread.h>
#  define bench_sleep_s(s)   sleep((unsigned)(s))
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

/* Aligned to 64 bytes to eliminate false sharing between shard cross-talks */
typedef struct {
    _Atomic(cache_item_data_t *) ptr;
} alignas(64) cache_bucket_t;

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
    uint64_t warmup_budget_ns;
} config_t;

static config_t cfg = { 
    .num_threads = 0, 
    .duration_sec = 300, 
    .arena_size = 1024 * 1024 * 128,
#if defined(__TINYC__)
    .write_shift = 12, 
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
    .max_probe = 4, /* Reduced probe depth since sharding optimizes density */
    .maintenance_scan = 4096,
    .warmup_ops = 5000,
    .warmup_budget_ns = 2ULL * 1000ULL * 1000ULL * 1000ULL
};
static const size_t kMaxArenaBudgetBytes = 1024ULL * 1024ULL * 1024ULL;

typedef struct {
    alignas(64) uint64_t read_ops;
    alignas(64) uint64_t write_ops;
    alignas(64) uint64_t hits;
    alignas(64) uint64_t misses;
    alignas(64) uint64_t expired;
    alignas(64) uint64_t evictions;
    alignas(64) uint64_t cleanups;
    alignas(64) uint64_t retirements;
    alignas(64) uint64_t total_ticks;
} stats_t;

static stats_t stats;
static uint64_t g_running = 1;
static uint64_t g_start_signal = 0;
static uint64_t g_threads_ready = 0;
static uint64_t g_abort_signal = 0;

static ttak_shared_t *g_cache;
static cache_table_t **g_lut_shards; /* Lookup Table of distributed sharded pointers */
static int g_shard_count = 0;
static uint32_t g_shard_mask = 0;

static ttak_epoch_gc_t g_gc;
static const uint32_t kStatFlushBatch = 4096;
static uint32_t g_write_mask = 5;
static uint32_t g_write_period = 100;
static uint32_t g_latency_mask = (1u << 3) - 1;
static uint32_t g_latency_scale = (1u << 3);

typedef struct {
    ttak_owner_t *owner;
    ttak_object_pool_t *arena;
    uint32_t write_accumulator;
    uint64_t rng_state;
    int thread_id; 
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
    g_write_period = (cfg.write_pct == 0) ? UINT32_MAX : 100;
}

static void bench_refresh_latency_mask(void) {
    if (cfg.latency_sample_shift == 0) {
        g_latency_mask = 0;
        g_latency_scale = 1;
        return;
    }
    if (cfg.latency_sample_shift > 15) cfg.latency_sample_shift = 15;
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
            buckets = 1ull << 16;
            break;
        }
    }
    size_t bytes = sizeof(cache_table_t) + buckets * sizeof(cache_bucket_t);
    cache_table_t *table = ttak_mem_alloc_raw(bytes, 0, ttak_get_tick_count());
    if (!table) return NULL;
    memset(table, 0, bytes);
    table->bucket_count = buckets;
    table->bucket_mask = buckets - 1;
    return table;
}

/* * Core Concept: Sharded LUT Routing via Key-Hashing.
 * Guarantees zero CAS contention by isolating core operations into distinct sub-tables.
 */
static inline cache_lookup_result_t cache_table_lookup_lut(uint64_t key, uint64_t now_ns) {
    cache_lookup_result_t result = { .kind = CACHE_RESULT_MISS, .entry = NULL, .item = NULL };
    uint64_t hash = bench_mix_u64(key);
    
    /* Route to dedicated table chunk in LUT array */
    cache_table_t *table = g_lut_shards[hash & g_shard_mask];
    size_t mask = table->bucket_mask;
    size_t idx = (hash >> 16) & mask; 
    uint32_t max_probe = cfg.max_probe ? cfg.max_probe : 1;
    
    for (uint32_t probe = 0; probe < max_probe; ++probe) {
        cache_item_data_t *payload = atomic_load_explicit(&table->buckets[idx].ptr, memory_order_acquire);
        
        if (!payload) return result;
        
        if (payload->key == key) {
            if (payload->expire_ns > now_ns) {
                result.kind = CACHE_RESULT_HIT;
                result.entry = cache_entry_from_payload(payload);
                result.item = payload;
                return result;
            }
            
            cache_item_data_t *expected = payload;
            if (atomic_compare_exchange_strong_explicit(&table->buckets[idx].ptr, &expected, NULL,
                                                       memory_order_release, memory_order_relaxed)) {
                ttak_epoch_retire(payload, retire_payload_cleanup);
                TTAK_FAST_ATOMIC_ADD_U64(&stats.retirements, 1);
                result.kind = CACHE_RESULT_EXPIRED;
                return result;
            }
            probe--; 
            continue;
        }
        idx = (idx + 1) & mask;
    }
    return result;
}

static inline cache_store_result_t cache_table_store_lut(cache_item_data_t *payload, uint64_t now_ns) {
    cache_store_result_t result = {0};
    uint64_t key = payload->key;
    uint64_t hash = bench_mix_u64(key);
    
    cache_table_t *table = g_lut_shards[hash & g_shard_mask];
    size_t mask = table->bucket_mask;
    size_t idx = (hash >> 16) & mask;
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
            probe--;
            continue;
        }
        idx = (idx + 1) & mask;
    }

    size_t last_idx = (((hash >> 16) & mask) + max_probe - 1) & mask;
    cache_item_data_t *existing = atomic_exchange_explicit(&table->buckets[last_idx].ptr, payload, memory_order_acq_rel);
    if (existing) {
        result.replaced = cache_entry_from_payload(existing);
        result.forced = true;
    }
    result.inserted = true;
    return result;
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
    return base + (bench_xorshift64(&ctx->rng_state) % span);
}

static inline void bench_fill_payload(cache_item_data_t *item, uint64_t key, uint64_t stamp) {
    uint64_t mix = bench_mix_u64(key ^ stamp);
    uint64_t *dst = (uint64_t *)item->value.data;
    for (size_t i = 0; i < (sizeof(item->value.data) / sizeof(uint64_t)); ++i) {
        dst[i] = mix + i;
    }
}

static bool bench_perform_write(worker_ctx_t *ctx, uint64_t key, uint64_t now_ns, 
                                uint64_t *local_evictions, uint64_t *local_retirements) {
    cache_entry_t *hdr = ttak_object_pool_alloc(ctx->arena);
    if (!hdr) return false;
    hdr->size = sizeof(cache_item_data_t);
    hdr->ts = now_ns;
    hdr->home_pool = ctx->arena;
    cache_item_data_t *item = (cache_item_data_t *)hdr->data;
    item->key = key;
    item->expire_ns = now_ns + cfg.ttl_ns;
    bench_fill_payload(item, key, now_ns);
    
    cache_store_result_t store_res = cache_table_store_lut(item, now_ns);
    if (!store_res.inserted) {
        ttak_object_pool_free(ctx->arena, hdr);
        return false;
    }
    if (store_res.replaced) {
        (*local_retirements)++;
        ttak_epoch_retire(store_res.replaced->data, retire_payload_cleanup);
    }
    if (store_res.forced) (*local_evictions)++;
    return true;
}

static void *worker_func(void *arg) {
    worker_ctx_t *ctx = (worker_ctx_t *)arg;
    ttak_epoch_register_thread();
    
    uint64_t local_reads = 0, local_writes = 0, local_hits = 0;
    uint64_t local_misses = 0, local_expired = 0, local_evictions = 0;
    uint64_t local_retired = 0, local_ticks = 0;
    uint32_t latency_counter = 0;
    uint8_t local_checksum = 0;

    uint32_t warmup_ops = cfg.warmup_ops ? cfg.warmup_ops : 5000;
    uint64_t warmup_deadline_ns = cfg.warmup_budget_ns;
    uint64_t warmup_start_ns = bench_now_ns();
    
    for (uint32_t i = 0; i < warmup_ops; ++i) {
        if (TTAK_FAST_ATOMIC_LOAD_U64(&g_abort_signal)) goto worker_cleanup;
        if (warmup_deadline_ns > 0 && (bench_now_ns() - warmup_start_ns) >= warmup_deadline_ns) break;
        
        uint64_t now_ns = bench_now_ns();
        uint64_t key = bench_select_key(ctx);
        ttak_epoch_enter();
        cache_lookup_result_t lookup = cache_table_lookup_lut(key, now_ns);
        if (lookup.kind == CACHE_RESULT_HIT && lookup.item) {
            volatile uint8_t *payload_bytes = (volatile uint8_t *)lookup.item->value.data;
            local_checksum ^= payload_bytes[i & (CACHE_PAYLOAD_BYTES - 1)];
        }
        ttak_epoch_exit();
        if (bench_consume_ops(ctx, 1)) {
            bench_perform_write(ctx, bench_select_key(ctx), bench_now_ns(), &local_evictions, &local_retired);
        }
    }

    local_reads = local_writes = local_hits = local_misses = local_expired = 0;
    local_evictions = local_retired = local_ticks = local_checksum = 0;

    TTAK_FAST_ATOMIC_ADD_U64(&g_threads_ready, 1);
    while (TTAK_FAST_ATOMIC_LOAD_U64(&g_start_signal) == 0) {
        if (TTAK_FAST_ATOMIC_LOAD_U64(&g_abort_signal)) goto worker_cleanup;
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
            if (!(TTAK_FAST_ATOMIC_LOAD_U64(&g_running) & 0xFF)) break;
            
            uint64_t now_ns = bench_now_ns();
            uint64_t key = bench_select_key(ctx);
            cache_lookup_result_t lookup = cache_table_lookup_lut(key, now_ns);
            
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
                if (bench_perform_write(ctx, write_key, bench_now_ns(), &local_evictions, &local_retired)) {
                    local_writes++;
                    ops_this_batch++;
                }
            }
        }
        ttak_epoch_exit();

        if (sample_tick && ops_this_batch) {
            uint64_t end = bench_read_cycles();
            local_ticks += bench_cycles_to_ns(end - start) * (uint64_t)g_latency_scale * ops_this_batch;
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
            local_reads = local_writes = local_hits = local_misses = local_expired = local_evictions = local_retired = local_ticks = 0;
        }
    }

worker_cleanup:
    TTAK_FAST_ATOMIC_ADD_U64(&stats.read_ops, local_reads);
    TTAK_FAST_ATOMIC_ADD_U64(&stats.write_ops, local_writes);
    TTAK_FAST_ATOMIC_ADD_U64(&stats.hits, local_hits);
    TTAK_FAST_ATOMIC_ADD_U64(&stats.misses, local_misses);
    TTAK_FAST_ATOMIC_ADD_U64(&stats.expired, local_expired);
    TTAK_FAST_ATOMIC_ADD_U64(&stats.evictions, local_evictions);
    TTAK_FAST_ATOMIC_ADD_U64(&stats.retirements, local_retired);
    TTAK_FAST_ATOMIC_ADD_U64(&stats.total_ticks, local_ticks);
    ttak_epoch_deregister_thread();
    return NULL;
}

/* Parallelized Shard Maintenance Scan with targeted affinity mapping */
static void *maintenance_task(void *arg) {
    (void)arg;
    ttak_epoch_register_thread();
    while (TTAK_FAST_ATOMIC_LOAD_U64(&g_start_signal) == 0) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile ("pause");
#endif
    }
    
    uint32_t shard_cursor = 0;
    while (TTAK_FAST_ATOMIC_LOAD_U64(&g_running) & 0xFF) {
        uint64_t now_ns = bench_now_ns();
        cache_table_t *table = g_lut_shards[shard_cursor & g_shard_mask];
        size_t budget = cfg.maintenance_scan ? cfg.maintenance_scan : 1024;
        
        if (table && budget > 0) {
            ttak_epoch_enter();
            size_t mask = table->bucket_mask;
            for (size_t i = 0; i < budget; ++i) {
                size_t idx = (i + now_ns) & mask;
                cache_item_data_t *payload = atomic_load_explicit(&table->buckets[idx].ptr, memory_order_acquire);
                if (payload && payload->expire_ns <= now_ns) {
                    cache_item_data_t *expected = payload;
                    if (atomic_compare_exchange_strong_explicit(&table->buckets[idx].ptr, &expected, NULL,
                                                               memory_order_release, memory_order_relaxed)) {
                        ttak_epoch_retire(payload, retire_payload_cleanup);
                        TTAK_FAST_ATOMIC_ADD_U64(&stats.cleanups, 1);
                        TTAK_FAST_ATOMIC_ADD_U64(&stats.retirements, 1);
                    }
                }
            }
            ttak_epoch_exit();
        }
        shard_cursor++;
    }
    ttak_epoch_reclaim();
    ttak_epoch_gc_rotate(&g_gc);
    ttak_epoch_deregister_thread();
    return NULL;
}

extern void ttak_epoch_subsystem_init(void);

static int detect_worker_threads(void) {
#ifdef _WIN32
    SYSTEM_INFO info; GetSystemInfo(&info); return (int)info.dwNumberOfProcessors;
#else
    long procs = sysconf(_SC_NPROCESSORS_ONLN); return (procs < 1) ? 1 : (int)procs;
#endif
}

int main(void) {
    int exit_code = 0;
    pthread_t *worker_threads = NULL;
    int workers_started = 0;
    pthread_t maintenance_thread;
    bool maintenance_started = false;
    
    ttak_epoch_subsystem_init();
#if BENCH_HAS_NATIVE_TSC
    calibrate_tsc();
#endif
    ttak_epoch_gc_init(&g_gc);
    
    size_t header_size = PAYLOAD_HEADER_BYTES; 
    size_t item_full_size = header_size + sizeof(cache_item_data_t);

    if (cfg.num_threads <= 0) {
        int detected = detect_worker_threads();
        int desired = detected > 1 ? detected - 1 : detected;
        if (desired < 2) desired = 2;
        cfg.num_threads = desired;
    }

    /* Set shard counts via Power-of-Two routing for instant Bitwise LUT masking */
    g_shard_count = 1;
    while (g_shard_count < cfg.num_threads) g_shard_count <<= 1;
    g_shard_mask = g_shard_count - 1;

    const char *threads_env = getenv("TTAK_BENCH_THREADS");
    if (threads_env && *threads_env) {
        int manual = atoi(threads_env);
        if (manual > 0) cfg.num_threads = manual;
    }
    
    /* Config parse options */
    const char *write_pct_env = getenv("TTAK_BENCH_WRITE_PCT");
    if (write_pct_env && *write_pct_env) cfg.write_pct = (uint32_t)atoi(write_pct_env);
    const char *table_env = getenv("TTAK_BENCH_TABLE_SIZE");
    if (table_env && *table_env) cfg.table_size = (size_t)atoll(table_env);
    
    if (cfg.table_size < 1024) cfg.table_size = 1024;
    bench_refresh_write_mask();
    bench_refresh_latency_mask();

    g_lut_shards = calloc(g_shard_count, sizeof(cache_table_t *));
    size_t shard_capacity = cfg.table_size / g_shard_count;
    if (shard_capacity < 512) shard_capacity = 512;

    for (int i = 0; i < g_shard_count; ++i) {
        g_lut_shards[i] = cache_table_create(shard_capacity);
        size_t table_bytes = sizeof(cache_table_t) + g_lut_shards[i]->bucket_count * sizeof(cache_bucket_t);
        pre_fault_memory(g_lut_shards[i], table_bytes);
    }

    g_workers = calloc((size_t)cfg.num_threads, sizeof(worker_ctx_t));
    size_t per_thread_bytes = cfg.arena_size;
    size_t arena_capacity = per_thread_bytes / item_full_size;

    for (int i = 0; i < cfg.num_threads; i++) {
        g_workers[i].arena = ttak_object_pool_create(arena_capacity, item_full_size);
        g_workers[i].thread_id = i;
        g_workers[i].write_accumulator = (uint32_t)(ttak_get_tick_count_ns() + i * 17u);
        pre_fault_memory(g_workers[i].arena->buffer, g_workers[i].arena->capacity * g_workers[i].arena->item_size);
        g_workers[i].rng_state = bench_mix_u64(ttak_get_tick_count_ns() ^ (i + 1) * 0x9e3779b97f4a7c15ULL);
    }

    g_cache = ttak_mem_alloc_raw(sizeof(ttak_shared_t), 0, ttak_get_tick_count());
    ttak_shared_init(g_cache);
    g_cache->allocate_typed(g_cache, sizeof(cache_item_data_t), "cache_item_data_t", TTAK_SHARED_NO_LEVEL);
    
    worker_threads = calloc((size_t)cfg.num_threads, sizeof(*worker_threads));
    for (int i = 0; i < cfg.num_threads; i++) {
        g_workers[i].owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
        g_cache->add_owner(g_cache, g_workers[i].owner);
        if (pthread_create(&worker_threads[i], NULL, worker_func, &g_workers[i]) != 0) {
            exit_code = 1; goto abort_workers;
        }
        workers_started++;
    }
    
    if (pthread_create(&maintenance_thread, NULL, maintenance_task, NULL) != 0) {
        exit_code = 1; goto abort_workers;
    }
    maintenance_started = true;

    printf("Warming up %d worker threads (LUT Shards: %d)...\n", cfg.num_threads, g_shard_count);
    while (TTAK_FAST_ATOMIC_LOAD_U64(&g_threads_ready) < (uint64_t)cfg.num_threads) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile ("pause");
#endif
        bench_usleep_us(1000);
    }

    printf("Workers: %d | LUT Shards = %d | write_pct=%u | batch=%u\n", cfg.num_threads, g_shard_count, cfg.write_pct, cfg.read_batch);
    printf("Time | Ops/s | Hit%% | Miss%% | Exp%% | Writes/s | Latency(ns) | RSS(KB) | Evict/s | Retire/s\n");
    printf("--------------------------------------------------------------------------------------------------\n");

    TTAK_FAST_ATOMIC_STORE_BOOL(&g_start_signal, 1);

    for (int i = 1; i <= cfg.duration_sec; i++) {
        bench_sleep_s(1);
        uint64_t reads = TTAK_FAST_ATOMIC_XCHG_U64(&stats.read_ops, 0);
        uint64_t writes = TTAK_FAST_ATOMIC_XCHG_U64(&stats.write_ops, 0);
        uint64_t hits = TTAK_FAST_ATOMIC_XCHG_U64(&stats.hits, 0);
        uint64_t misses = TTAK_FAST_ATOMIC_XCHG_U64(&stats.misses, 0);
        uint64_t expired = TTAK_FAST_ATOMIC_XCHG_U64(&stats.expired, 0);
        uint64_t evictions = TTAK_FAST_ATOMIC_XCHG_U64(&stats.evictions, 0);
        uint64_t retired = TTAK_FAST_ATOMIC_XCHG_U64(&stats.retirements, 0);
        uint64_t ticks = TTAK_FAST_ATOMIC_XCHG_U64(&stats.total_ticks, 0);
        uint64_t ops = reads + writes;
        uint64_t lat = (ops > 0) ? (ticks / ops) : 0;
        
        printf("%2ds | %8" PRIu64 " | %6.2f | %6.2f | %6.2f | %8" PRIu64 " | %11" PRIu64 " | %7ld | %7" PRIu64 " | %9" PRIu64 "\n",
               i, ops, (reads > 0) ? (hits * 100.0 / reads) : 0.0,
               (reads > 0) ? (misses * 100.0 / reads) : 0.0,
               (reads > 0) ? (expired * 100.0 / reads) : 0.0, writes, lat, get_rss_kb(), evictions, retired);
        fflush(stdout);
    }

    TTAK_FAST_ATOMIC_STORE_BOOL(&g_running, 0);
    goto cleanup;

abort_workers:
    TTAK_FAST_ATOMIC_STORE_BOOL(&g_abort_signal, 1);
    TTAK_FAST_ATOMIC_STORE_BOOL(&g_running, 0);
    TTAK_FAST_ATOMIC_STORE_BOOL(&g_start_signal, 1);

cleanup:
    if (maintenance_started) pthread_join(maintenance_thread, NULL);
    for (int i = 0; i < workers_started; ++i) pthread_join(worker_threads[i], NULL);
    free(worker_threads);
    return exit_code;
}
