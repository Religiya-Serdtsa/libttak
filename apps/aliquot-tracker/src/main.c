/**
 * @file main.c
 * @brief Standalone aliquot tracker that seeds exploratory jobs, schedules work,
 * and records interesting findings to disk.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <stdalign.h>
#include <limits.h>

#include <ttak/mem/mem.h>
#include <ttak/mem/owner.h>
#include <ttak/sync/sync.h>
#include <ttak/timing/timing.h>
#include <ttak/atomic/atomic.h>
#include <ttak/math/bigint.h>
#include <ttak/math/factor.h>
#include <ttak/math/sum_divisors.h>
#include <ttak/thread/pool.h>

#ifndef PATH_MAX
#define PATH_MAX 8192
#endif

#define STATE_ENV_VAR       "ALIQUOT_STATE_DIR"
#define DEFAULT_STATE_DIR   "/opt/aliquot-tracker"
#define FOUND_LOG_NAME      "aliquot_found.jsonl"
#define JUMP_LOG_NAME       "aliquot_jump.jsonl"
#define TRACK_LOG_NAME      "aliquot_track.jsonl"
#define CATALOG_FILTER_FILE "catalog_filters.txt"
#define QUEUE_STATE_NAME    "aliquot_queue.json"
#define LEDGER_RESOURCE_NAME "ledger-state"

#define MAX_WORKERS         8
#define JOB_QUEUE_CAP       512
#define HISTORY_BUCKETS     8192
#define LONG_RUN_MAX_STEPS  100000
#define SCOUT_PREVIEW_STEPS 256
#define FLUSH_INTERVAL_MS   4000
#define SCOUT_SLEEP_MS      50
#define SCOUT_MIN_SEED      1000ULL
#define SCOUT_MAX_SEED      50000000ULL
#define SCOUT_SCORE_GATE    120.0
#define SCAN_STEP_CAP       64
#define SCAN_TIMECAP_MS     25
#define TRACK_PREFIX_DIGITS 48
#define TRACK_FAST_BUDGET_MS (30ULL * 60ULL * 1000ULL)
#define TRACK_DEEP_BUDGET_MS (365ULL * 24ULL * 60ULL * 60ULL * 1000ULL)
#define CATALOG_MAX_EXACT    512
#define CATALOG_MAX_MOD_RULE 256
#define SEED_REGISTRY_BUCKETS 65536

typedef struct {
    ttak_bigint_t seed;
    uint64_t steps;
    ttak_bigint_t max_value;
    ttak_bigint_t final_value;
    uint32_t cycle_length;
    bool terminated;
    bool entered_cycle;
    bool amicable;
    bool perfect;
    bool overflow;
    bool hit_limit;
    bool time_budget_hit;
    bool catalog_hit;
    uint64_t wall_time_ms;
    uint64_t wall_time_us;
    uint32_t max_bits;
    uint32_t max_step_index;
    char *max_value_dec;
    char max_hash[65];
    char max_prefix[TRACK_PREFIX_DIGITS + 1];
    uint32_t max_dec_digits;
} aliquot_outcome_t;

typedef struct {
    ttak_bigint_t seed;
    uint64_t steps;
    ttak_bigint_t max_value;
    ttak_bigint_t final_value;
    uint32_t cycle_length;
    char status[24];
    char provenance[16];
} found_record_t;

typedef struct {
    ttak_bigint_t seed;
    uint64_t preview_steps;
    ttak_bigint_t preview_max;
    double score;
    double overflow_pressure;
} jump_record_t;

typedef enum {
    SCAN_END_CATALOG = 0,
    SCAN_END_OVERFLOW,
    SCAN_END_TIMECAP
} scan_end_reason_t;

typedef struct {
    ttak_bigint_t seed;
    uint64_t steps;
    ttak_bigint_t max_value;
    scan_end_reason_t ended_by;
} scan_result_t;

typedef struct {
    uint64_t modulus;
    uint64_t remainder;
} catalog_mod_rule_t;

typedef struct {
    ttak_bigint_t seed;
    uint64_t steps;
    uint64_t wall_time_ms;
    uint64_t wall_time_us;
    uint64_t budget_ms;
    uint32_t max_step;
    uint32_t max_bits;
    uint32_t max_dec_digits;
    double scout_score;
    uint32_t priority;
    char ended[24];
    char ended_by[32];
    char max_hash[65];
    char max_prefix[TRACK_PREFIX_DIGITS + 1];
    char *max_value_dec;
} track_record_t;

typedef struct history_entry {
    uint64_t value;
    uint32_t step;
    struct history_entry *next;
} history_entry_t;

typedef struct {
    history_entry_t *buckets[HISTORY_BUCKETS];
} history_table_t;

typedef struct history_big_entry {
    uint8_t hash[32];
    uint32_t step;
    struct history_big_entry *next;
} history_big_entry_t;

typedef struct {
    history_big_entry_t *buckets[HISTORY_BUCKETS];
} history_big_table_t;

typedef struct {
    ttak_bigint_t seed;
    char provenance[16];
    uint32_t priority;
    double scout_score;
    uint64_t preview_steps;
    ttak_bigint_t preview_max;
    bool preview_overflow;
} aliquot_job_t;

typedef struct seed_entry {
    ttak_bigint_t seed;
    struct seed_entry *next;
} seed_entry_t;

typedef struct {
    ttak_bigint_t seeds[JOB_QUEUE_CAP];
    size_t count;
} pending_queue_t;

typedef struct {
    found_record_t *found_records;
    size_t found_count;
    size_t found_cap;
    size_t persisted_found_count;

    jump_record_t *jump_records;
    size_t jump_count;
    size_t jump_cap;
    size_t persisted_jump_count;

    track_record_t *track_records;
    size_t track_count;
    size_t track_cap;
    size_t persisted_track_count;

    ttak_mutex_t lock;
} ledger_state_t;

static volatile uint64_t shutdown_requested;
static volatile uint64_t g_rng_state;

static char g_state_dir[PATH_MAX];
static char g_found_log_path[PATH_MAX];
static char g_jump_log_path[PATH_MAX];
static char g_track_log_path[PATH_MAX];
static char g_queue_state_path[PATH_MAX];

static seed_entry_t *g_seed_buckets[SEED_REGISTRY_BUCKETS];
static ttak_mutex_t g_seed_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t g_catalog_exact[CATALOG_MAX_EXACT];
static size_t g_catalog_exact_count;
static catalog_mod_rule_t g_catalog_mod_rules[CATALOG_MAX_MOD_RULE];
static size_t g_catalog_mod_rule_count;

static void aliquot_outcome_cleanup(aliquot_outcome_t *out);
static bool aliquot_outcome_set_decimal_from_bigint(aliquot_outcome_t *out, const ttak_bigint_t *value, uint64_t now);

static const uint64_t g_catalog_seeds[] = {
    1, 2, 3, 4, 5, 6, 28, 496, 8128, 33550336,
    8589869056ULL, 137438691328ULL,
    1184, 1210, 2620, 2924, 5020, 5564, 6232, 6368,
    10744, 10856, 12285, 14595, 17296, 18416,
    24608, 27664, 45872, 45946, 66928, 66992,
    67095, 71145, 69615, 87633, 100485, 124155,
    122265, 139815, 141664, 153176, 142310, 168730,
    171856, 176336, 180848, 185368, 196724, 202444,
    280540, 365084, 308620, 389924, 418904, 748210,
    823816, 876960, 998104, 1154450, 1189800, 1866152,
    2082464, 2236570, 2652728, 2723792, 5224050, 5947064,
    6086552, 6175984
};

static ledger_state_t g_ledger_state;
static ttak_owner_t *g_ledger_owner;

static pending_queue_t g_pending_queue;
static ttak_mutex_t g_pending_lock = PTHREAD_MUTEX_INITIALIZER;

static ttak_mutex_t g_disk_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_last_persist_ms;
static uint64_t g_total_sequences;
static uint64_t g_total_probes;
static uint64_t g_probe_update_quantum = 1;
#if defined(ENABLE_CUDA) || defined(ENABLE_ROCM) || defined(ENABLE_OPENCL)
static uint64_t g_gpu_rate_last_sync_ms;
static uint64_t g_gpu_rate_last_sequences;
static uint64_t g_gpu_rate_bits;
#endif

static ttak_thread_pool_t *g_thread_pool;

static double compute_overflow_pressure(const aliquot_outcome_t *out);
static void *worker_process_job_wrapper(void *arg);
static void process_job(const aliquot_job_t *job);
static bool enqueue_job(aliquot_job_t *job, const char *source_tag);
static bool ledger_init_owner(void);
static void ledger_destroy_owner(void);
static bool ledger_store_found_record(const found_record_t *rec);
static bool ledger_store_jump_record(const jump_record_t *rec);
static bool ledger_store_track_record(const track_record_t *rec);
static void ledger_mark_found_persisted(void);
static void ledger_mark_jump_persisted(void);
static void ledger_mark_track_persisted(void);

static bool pending_queue_add(const ttak_bigint_t *seed) {
    ttak_mutex_lock(&g_pending_lock);
    if (g_pending_queue.count >= JOB_QUEUE_CAP) {
        ttak_mutex_unlock(&g_pending_lock);
        return false;
    }
    ttak_bigint_init_copy(&g_pending_queue.seeds[g_pending_queue.count++], seed, ttak_get_tick_count());
    ttak_mutex_unlock(&g_pending_lock);
    return true;
}

static void pending_queue_remove(const ttak_bigint_t *seed) {
    ttak_mutex_lock(&g_pending_lock);
    for (size_t i = 0; i < g_pending_queue.count; ++i) {
        if (ttak_bigint_cmp(&g_pending_queue.seeds[i], seed) == 0) {
            ttak_bigint_free(&g_pending_queue.seeds[i], ttak_get_tick_count());
            g_pending_queue.seeds[i] = g_pending_queue.seeds[g_pending_queue.count - 1];
            g_pending_queue.count--;
            break;
        }
    }
    ttak_mutex_unlock(&g_pending_lock);
}

static size_t pending_queue_snapshot(ttak_bigint_t *dest, size_t cap) {
    if (!dest || cap == 0) return 0;
    ttak_mutex_lock(&g_pending_lock);
    size_t count = g_pending_queue.count;
    if (count > cap) count = cap;
    for (size_t i = 0; i < count; ++i) {
        ttak_bigint_init_copy(&dest[i], &g_pending_queue.seeds[i], ttak_get_tick_count());
    }
    ttak_mutex_unlock(&g_pending_lock);
    return count;
}

static size_t pending_queue_depth(void) {
    ttak_mutex_lock(&g_pending_lock);
    size_t count = g_pending_queue.count;
    ttak_mutex_unlock(&g_pending_lock);
    return count;
}

static void handle_signal(int sig) {
    (void)sig;
    ttak_atomic_write64(&shutdown_requested, 1);
}

static uint64_t monotonic_millis(void) {
    return ttak_get_tick_count();
}

static uint64_t monotonic_micros(void) {
    return ttak_get_tick_count_ns() / 1000ULL;
}

static void responsive_sleep(uint32_t ms) {
    const uint32_t chunk = 200;
    uint32_t waited = 0;
    while (waited < ms) {
        if (ttak_atomic_read64(&shutdown_requested)) break;
        uint32_t slice = ms - waited;
        if (slice > chunk) slice = chunk;
        struct timespec ts = {.tv_sec = slice / 1000, .tv_nsec = (slice % 1000) * 1000000L};
        nanosleep(&ts, NULL);
        waited += slice;
    }
}

static bool join_state_path(char *dest, size_t dest_size, const char *dir, const char *leaf) {
    if (!dest || !dir || !leaf || dest_size == 0) return false;
    size_t dir_len = strnlen(dir, dest_size);
    if (dir_len >= dest_size) return false;
    size_t leaf_len = strlen(leaf);
    bool needs_sep = dir_len > 0 && dir[dir_len - 1] != '/';
    size_t total = dir_len + (needs_sep ? 1 : 0) + leaf_len + 1;
    if (total > dest_size) return false;

    size_t pos = 0;
    if (dir_len) {
        memcpy(dest, dir, dir_len);
        pos = dir_len;
    } else dest[0] = '\0';

    if (needs_sep) dest[pos++] = '/';
    memcpy(dest + pos, leaf, leaf_len);
    pos += leaf_len;
    dest[pos] = '\0';
    return true;
}

static void configure_state_paths(void) {
    const char *override = getenv(STATE_ENV_VAR);
    const char *base = (override && *override) ? override : DEFAULT_STATE_DIR;
    snprintf(g_state_dir, sizeof(g_state_dir), "%s", base);
    g_state_dir[sizeof(g_state_dir) - 1] = '\0';
    size_t len = strlen(g_state_dir);
    while (len > 1 && g_state_dir[len - 1] == '/') {
        g_state_dir[--len] = '\0';
    }
    if (len == 0) {
        snprintf(g_state_dir, sizeof(g_state_dir), "%s", DEFAULT_STATE_DIR);
    }
    if (!join_state_path(g_found_log_path, sizeof(g_found_log_path), g_state_dir, FOUND_LOG_NAME) ||
        !join_state_path(g_jump_log_path, sizeof(g_jump_log_path), g_state_dir, JUMP_LOG_NAME) ||
        !join_state_path(g_track_log_path, sizeof(g_track_log_path), g_state_dir, TRACK_LOG_NAME) ||
        !join_state_path(g_queue_state_path, sizeof(g_queue_state_path), g_state_dir, QUEUE_STATE_NAME)) {
        fprintf(stderr, "[ALIQUOT] State path too long, please choose a shorter %s\n", STATE_ENV_VAR);
        exit(1);
    }
}

static void seed_rng(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t seed = ((uint64_t)ts.tv_nsec << 16) ^ (uint64_t)getpid();
    if (seed == 0) seed = 88172645463393265ULL;
    ttak_atomic_write64(&g_rng_state, seed);
}

static uint64_t next_random64(void) {
    uint64_t x = ttak_atomic_read64(&g_rng_state);
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    x *= 2685821657736338717ULL;
    ttak_atomic_write64(&g_rng_state, x);
    return x;
}

static uint64_t random_seed_between(uint64_t lo, uint64_t hi) {
    if (hi <= lo) return lo;
    uint64_t span = hi - lo + 1ULL;
    return lo + (next_random64() % span);
}

static void configure_probe_quantum(void) {
    uint64_t quant = 1;
#if defined(ENABLE_CUDA) || defined(ENABLE_ROCM) || defined(ENABLE_OPENCL)
    quant = 1;
#endif
    const char *override = getenv("ALIQUOT_RATE_QUANTUM");
    if (override && *override) {
        char *endp = NULL;
        unsigned long long parsed = strtoull(override, &endp, 10);
        if (endp && *endp == '\0' && parsed > 0) {
            quant = (uint64_t)parsed;
        }
    }
    if (quant == 0) {
        quant = 1;
    }
    g_probe_update_quantum = quant;
}

static inline void tracker_probe_note(uint64_t *pending, uint64_t delta) {
    if (!pending) return;
    uint64_t quantum = g_probe_update_quantum ? g_probe_update_quantum : 1;
    *pending += delta;
    if (*pending >= quantum) {
        ttak_atomic_add64(&g_total_probes, *pending);
        *pending = 0;
    }
}

static inline void tracker_probe_flush(uint64_t *pending) {
    if (!pending || *pending == 0) return;
    ttak_atomic_add64(&g_total_probes, *pending);
    *pending = 0;
}

static void ensure_state_dir(void) {
    struct stat st;
    if (stat(g_state_dir, &st) == 0 && S_ISDIR(st.st_mode)) return;
    if (mkdir(g_state_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "[ALIQUOT] Failed to create %s: %s\n", g_state_dir, strerror(errno));
    }
}

static bool seed_registry_try_add(const ttak_bigint_t *seed) {
    uint8_t hash[32];
    ttak_bigint_hash(seed, hash);
    uint64_t hash_prefix;
    memcpy(&hash_prefix, hash, sizeof(hash_prefix));
    size_t idx = hash_prefix % SEED_REGISTRY_BUCKETS;
    
    ttak_mutex_lock(&g_seed_lock);
    seed_entry_t *node = g_seed_buckets[idx];
    while (node) {
        if (ttak_bigint_cmp(&node->seed, seed) == 0) {
            ttak_mutex_unlock(&g_seed_lock);
            return false;
        }
        node = node->next;
    }
    uint64_t now = monotonic_millis();
    node = ttak_mem_alloc(sizeof(*node), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!node) {
        ttak_mutex_unlock(&g_seed_lock);
        return false;
    }
    ttak_bigint_init_copy(&node->seed, seed, now);
    node->next = g_seed_buckets[idx];
    g_seed_buckets[idx] = node;
    ttak_mutex_unlock(&g_seed_lock);
    return true;
}

static void seed_registry_mark(const ttak_bigint_t *seed) {
    (void)seed_registry_try_add(seed);
}

static void history_big_init(history_big_table_t *t) {
    memset(t, 0, sizeof(*t));
}

static void history_big_destroy(history_big_table_t *t) {
    for (size_t i = 0; i < HISTORY_BUCKETS; ++i) {
        history_big_entry_t *node = t->buckets[i];
        while (node) {
            history_big_entry_t *next = node->next;
            ttak_mem_free(node);
            node = next;
        }
        t->buckets[i] = NULL;
    }
}

static bool history_big_contains(history_big_table_t *t, const ttak_bigint_t *value, uint32_t *step_out) {
    uint8_t hash[32];
    ttak_bigint_hash(value, hash);
    uint64_t hash_prefix;
    memcpy(&hash_prefix, hash, sizeof(hash_prefix));
    size_t idx = hash_prefix % HISTORY_BUCKETS;
    history_big_entry_t *node = t->buckets[idx];
    while (node) {
        if (memcmp(node->hash, hash, 32) == 0) {
            if (step_out) *step_out = node->step;
            return true;
        }
        node = node->next;
    }
    return false;
}

static void history_big_insert(history_big_table_t *t, const ttak_bigint_t *value, uint32_t step) {
    uint8_t hash[32];
    ttak_bigint_hash(value, hash);
    uint64_t hash_prefix;
    memcpy(&hash_prefix, hash, sizeof(hash_prefix));
    size_t idx = hash_prefix % HISTORY_BUCKETS;
    uint64_t now = monotonic_millis();
    history_big_entry_t *node = ttak_mem_alloc(sizeof(*node), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!node) return;
    memcpy(node->hash, hash, 32);
    node->step = step;
    node->next = t->buckets[idx];
    t->buckets[idx] = node;
}

static bool record_catalog_exact(uint64_t seed) {
    for (size_t i = 0; i < g_catalog_exact_count; ++i) {
        if (g_catalog_exact[i] == seed) return true;
    }
    if (g_catalog_exact_count >= CATALOG_MAX_EXACT) return false;
    g_catalog_exact[g_catalog_exact_count++] = seed;
    return true;
}

static bool record_catalog_mod(uint64_t modulus, uint64_t remainder) {
    if (modulus == 0) return false;
    for (size_t i = 0; i < g_catalog_mod_rule_count; ++i) {
        if (g_catalog_mod_rules[i].modulus == modulus &&
            g_catalog_mod_rules[i].remainder == remainder) {
            return true;
        }
    }
    if (g_catalog_mod_rule_count >= CATALOG_MAX_MOD_RULE) return false;
    g_catalog_mod_rules[g_catalog_mod_rule_count++] = (catalog_mod_rule_t){
        .modulus = modulus,
        .remainder = remainder
    };
    return true;
}

static void load_catalog_filter_file(void) {
    char path[PATH_MAX + 64];
    snprintf(path, sizeof(path), "%s/%s", g_state_dir, CATALOG_FILTER_FILE);
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *ptr = line;
        while (isspace((unsigned char)*ptr)) ptr++;
        if (*ptr == '\0' || *ptr == '#') continue;
        uint64_t a = 0, b = 0;
        if (sscanf(ptr, "exact:%" SCNu64, &a) == 1 ||
            sscanf(ptr, "exact=%" SCNu64, &a) == 1) {
            record_catalog_exact(a);
            continue;
        }
        if (sscanf(ptr, "mod:%" SCNu64 ":%" SCNu64, &a, &b) == 2 ||
            sscanf(ptr, "mod=%" SCNu64 ":%" SCNu64, &a, &b) == 2) {
            record_catalog_mod(a, b % a);
            continue;
        }
    }
    fclose(fp);
}

static void init_catalog_filters(void) {
    g_catalog_exact_count = 0;
    g_catalog_mod_rule_count = 0;
    for (size_t i = 0; i < sizeof(g_catalog_seeds) / sizeof(g_catalog_seeds[0]); ++i) {
        record_catalog_exact(g_catalog_seeds[i]);
    }
    load_catalog_filter_file();
}

static bool is_catalog_value(const ttak_bigint_t *value) {
    uint64_t u64;
    bool fits = ttak_bigint_export_u64(value, &u64);
    if (fits) {
        for (size_t i = 0; i < g_catalog_exact_count; ++i) {
            if (g_catalog_exact[i] == u64) return true;
        }
    }
    for (size_t i = 0; i < g_catalog_mod_rule_count; ++i) {
        const catalog_mod_rule_t *rule = &g_catalog_mod_rules[i];
        if (rule->modulus) {
            ttak_bigint_t r;
            ttak_bigint_init(&r, monotonic_millis());
            if (ttak_bigint_mod_u64(&r, value, rule->modulus, monotonic_millis())) {
                uint64_t rem;
                if (ttak_bigint_export_u64(&r, &rem) && rem == rule->remainder) {
                    ttak_bigint_free(&r, monotonic_millis());
                    return true;
                }
            }
            ttak_bigint_free(&r, monotonic_millis());
        }
    }
    return false;
}

static const char *classify_outcome(const aliquot_outcome_t *out) {
    if (out->max_bits > 64) {
        if (out->entered_cycle) return "big-cycle";
        if (out->terminated) return "big-terminated";
        if (out->hit_limit) return "big-open-limit";
        return "big-open";
    }
    if (out->overflow) return "overflow";
    if (out->catalog_hit) return "catalog";
    if (out->perfect) return "perfect";
    if (out->amicable) return "amicable";
    if (out->terminated) return "terminated";
    if (out->entered_cycle) return "cycle";
    if (out->hit_limit) return "open-limit";
    return "open";
}

static bool frontier_accept_seed(const ttak_bigint_t *seed, scan_result_t *result) {
    uint64_t now = monotonic_millis();
    if (result) {
        memset(result, 0, sizeof(*result));
        ttak_bigint_init_copy(&result->seed, seed, now);
        ttak_bigint_init_copy(&result->max_value, seed, now);
    }
    
    // We record everything now, but still check catalog for metadata if needed.
    // However, we don't return false just because it's in catalog if we want to record it.
    // For now, let's keep the catalog check but maybe return true anyway to ensure recording?
    // User said "애초에 작은 수부터도 모든 재현가능해야 할 기록들을 기록하며"
    if (is_catalog_value(seed)) {
        if (result) result->ended_by = SCAN_END_CATALOG;
        // return false; // Don't return false, let it be processed if it's a new seed.
    }

    uint64_t start_ms = monotonic_millis();
    ttak_bigint_t current;
    ttak_bigint_init_copy(&current, seed, now);
    ttak_bigint_t max_value;
    ttak_bigint_init_copy(&max_value, seed, now);
    
    uint32_t steps = 0;
    bool accepted = true;
    uint64_t probe_progress = 0;

    while (steps < SCAN_STEP_CAP) {
        if (ttak_atomic_read64(&shutdown_requested)) break;
        if (SCAN_TIMECAP_MS > 0) {
            if (monotonic_millis() - start_ms >= (uint64_t)SCAN_TIMECAP_MS) break;
        }
        
        ttak_bigint_t next;
        ttak_bigint_init(&next, monotonic_millis());
        bool ok = ttak_sum_proper_divisors_big(&current, &next, monotonic_millis());
        tracker_probe_note(&probe_progress, 1);
        steps++;

        if (!ok) {
            if (result) {
                result->ended_by = SCAN_END_OVERFLOW;
                result->steps = steps;
                ttak_bigint_copy(&result->max_value, &max_value, monotonic_millis());
            }
            ttak_bigint_free(&next, monotonic_millis());
            accepted = true;
            goto cleanup;
        }

        if (ttak_bigint_cmp(&next, &max_value) > 0) {
            ttak_bigint_copy(&max_value, &next, monotonic_millis());
        }

        if (is_catalog_value(&next)) {
            if (result) {
                result->ended_by = SCAN_END_CATALOG;
                result->steps = steps;
                ttak_bigint_copy(&result->max_value, &max_value, monotonic_millis());
            }
            // Even if it hits catalog, we might want to continue or just mark it.
            // For frontier scan, hitting catalog means it's a known path.
            // accepted = false; 
            // goto cleanup;
        }
        ttak_bigint_copy(&current, &next, monotonic_millis());
        ttak_bigint_free(&next, monotonic_millis());
    }

    if (result) {
        result->ended_by = SCAN_END_TIMECAP;
        result->steps = steps;
        ttak_bigint_copy(&result->max_value, &max_value, monotonic_millis());
    }

cleanup:
    ttak_bigint_free(&current, monotonic_millis());
    ttak_bigint_free(&max_value, monotonic_millis());
    tracker_probe_flush(&probe_progress);
    return accepted;
}

static void run_aliquot_sequence(const ttak_bigint_t *seed, uint32_t max_steps, uint64_t time_budget_ms, aliquot_outcome_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    uint64_t now = monotonic_millis();
    ttak_bigint_init_copy(&out->seed, seed, now);
    ttak_bigint_init_copy(&out->max_value, seed, now);
    ttak_bigint_init_copy(&out->final_value, seed, now);
    out->max_bits = (uint32_t)ttak_bigint_get_bit_length(seed);
    out->max_step_index = 0;
    
    uint64_t start_ms = monotonic_millis();
    uint64_t start_us = monotonic_micros();

    history_big_table_t hist;
    history_big_init(&hist);
    history_big_insert(&hist, seed, 0);

    ttak_bigint_t current;
    ttak_bigint_init_copy(&current, seed, now);
    uint32_t steps = 0;
    uint64_t probe_progress = 0;

    while (true) {
        if (steps == 0 && is_catalog_value(&current)) {
            out->catalog_hit = true;
            // We still want to record the catalog hit, but maybe we should continue one step
            // to see what it is? Actually perfect numbers would cycle immediately.
        }

        if (max_steps > 0 && steps >= max_steps) {
            out->hit_limit = true;
            break;
        }
        if (time_budget_ms > 0) {
            if (monotonic_millis() - start_ms >= time_budget_ms) {
                out->hit_limit = true;
                out->time_budget_hit = true;
                break;
            }
        }
        if (ttak_atomic_read64(&shutdown_requested)) break;

        ttak_bigint_t next;
        ttak_bigint_init(&next, monotonic_millis());
        bool ok = ttak_sum_proper_divisors_big(&current, &next, monotonic_millis());
        tracker_probe_note(&probe_progress, 1);
        steps++;

        if (!ok) {
            out->overflow = true;
            ttak_bigint_free(&next, monotonic_millis());
            break;
        }

        if (ttak_bigint_cmp(&next, &out->max_value) > 0) {
            ttak_bigint_copy(&out->max_value, &next, monotonic_millis());
            out->max_bits = (uint32_t)ttak_bigint_get_bit_length(&out->max_value);
            out->max_step_index = steps;
        }

        if (ttak_bigint_is_zero(&next) || ttak_bigint_cmp_u64(&next, 1) == 0) {
            out->terminated = true;
            ttak_bigint_copy(&out->final_value, &next, monotonic_millis());
            ttak_bigint_free(&next, monotonic_millis());
            break;
        }

        uint32_t prev_step = 0;
        if (history_big_contains(&hist, &next, &prev_step)) {
            out->entered_cycle = true;
            out->cycle_length = steps - prev_step;
            ttak_bigint_copy(&out->final_value, &next, monotonic_millis());
            if (out->cycle_length <= 2) {
                if (out->cycle_length == 1 && ttak_bigint_cmp(&next, &current) == 0) out->perfect = true;
                else out->amicable = true;
            }
            ttak_bigint_free(&next, monotonic_millis());
            break;
        }

        if (is_catalog_value(&next)) {
            out->catalog_hit = true;
            ttak_bigint_copy(&out->final_value, &next, monotonic_millis());
            // Should we stop here? Catalog hit means we know the end.
            ttak_bigint_free(&next, monotonic_millis());
            break;
        }

        history_big_insert(&hist, &next, steps);
        ttak_bigint_copy(&current, &next, monotonic_millis());
        ttak_bigint_free(&next, monotonic_millis());
    }

    if (!out->terminated && !out->entered_cycle && !out->overflow && !out->catalog_hit) {
        ttak_bigint_copy(&out->final_value, &current, now);
    }
    out->steps = steps;
    uint64_t elapsed_us = monotonic_micros() - start_us;
    out->wall_time_us = elapsed_us;
    out->wall_time_ms = elapsed_us / 1000ULL;
    if (out->wall_time_ms == 0 && elapsed_us > 0) {
        out->wall_time_ms = 1;
    }

    ttak_bigint_to_hex_hash(&out->max_value, out->max_hash);
    ttak_bigint_format_prefix(&out->max_value, out->max_prefix, sizeof(out->max_prefix));
    aliquot_outcome_set_decimal_from_bigint(out, &out->max_value, monotonic_millis());

    ttak_bigint_free(&current, monotonic_millis());
    tracker_probe_flush(&probe_progress);
    history_big_destroy(&hist);
}

static double compute_probe_score(const aliquot_outcome_t *out) {
    double span_log2 = (double)ttak_bigint_get_bit_length(&out->max_value) - (double)ttak_bigint_get_bit_length(&out->seed);
    if (span_log2 < 0) span_log2 = 0;
    double base = (double)out->steps * 0.75 + span_log2 * 5.0;
    if (out->hit_limit) base += 30.0;
    if (ttak_bigint_cmp_u64(&out->max_value, 1000000000ULL) > 0) base += 25.0;
    base += compute_overflow_pressure(out);
    return base;
}

static bool looks_long(const aliquot_outcome_t *out, double *score_out) {
    double score = compute_probe_score(out);
    if (score_out) *score_out = score;
    if (out->terminated || out->entered_cycle || out->overflow) return false;
    return score >= SCOUT_SCORE_GATE;
}

static double compute_overflow_pressure(const aliquot_outcome_t *out) {
    if (!out) return 0.0;
    if (out->overflow) return 60.0;
    uint32_t bits = (uint32_t)ttak_bigint_get_bit_length(&out->max_value);
    if (bits >= 1024) return 60.0;
    return (double)bits * 60.0 / 1024.0;
}

static bool ledger_ensure_found_capacity_locked(ledger_state_t *state, size_t extra) {
    if (!state) return false;
    if (state->found_count + extra <= state->found_cap) return true;
    size_t new_cap = state->found_cap ? state->found_cap * 2 : 32;
    while (new_cap < state->found_count + extra) new_cap *= 2;
    size_t bytes = new_cap * sizeof(*state->found_records);
    uint64_t now = monotonic_millis();
    found_record_t *tmp = state->found_records
        ? ttak_mem_realloc(state->found_records, bytes, __TTAK_UNSAFE_MEM_FOREVER__, now)
        : ttak_mem_alloc(bytes, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!tmp) return false;
    state->found_records = tmp;
    state->found_cap = new_cap;
    return true;
}

static bool ledger_ensure_jump_capacity_locked(ledger_state_t *state, size_t extra) {
    if (!state) return false;
    if (state->jump_count + extra <= state->jump_cap) return true;
    size_t new_cap = state->jump_cap ? state->jump_cap * 2 : 32;
    while (new_cap < state->jump_count + extra) new_cap *= 2;
    size_t bytes = new_cap * sizeof(*state->jump_records);
    uint64_t now = monotonic_millis();
    jump_record_t *tmp = state->jump_records
        ? ttak_mem_realloc(state->jump_records, bytes, __TTAK_UNSAFE_MEM_FOREVER__, now)
        : ttak_mem_alloc(bytes, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!tmp) return false;
    state->jump_records = tmp;
    state->jump_cap = new_cap;
    return true;
}

static bool ledger_ensure_track_capacity_locked(ledger_state_t *state, size_t extra) {
    if (!state) return false;
    if (state->track_count + extra <= state->track_cap) return true;
    size_t new_cap = state->track_cap ? state->track_cap * 2 : 32;
    while (new_cap < state->track_count + extra) new_cap *= 2;
    size_t bytes = new_cap * sizeof(*state->track_records);
    uint64_t now = monotonic_millis();
    track_record_t *tmp = state->track_records
        ? ttak_mem_realloc(state->track_records, bytes, __TTAK_UNSAFE_MEM_FOREVER__, now)
        : ttak_mem_alloc(bytes, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!tmp) return false;
    state->track_records = tmp;
    state->track_cap = new_cap;
    return true;
}

typedef struct {
    const found_record_t *record;
    bool ok;
} ledger_store_found_args_t;

typedef struct {
    const jump_record_t *record;
    bool ok;
} ledger_store_jump_args_t;

typedef struct {
    const track_record_t *record;
    bool ok;
} ledger_store_track_args_t;

static void ledger_owner_store_found(void *ctx, void *args) {
    ledger_state_t *state = (ledger_state_t *)ctx;
    ledger_store_found_args_t *params = (ledger_store_found_args_t *)args;
    if (!state || !params || !params->record) return;
    ttak_mutex_lock(&state->lock);
    if (ledger_ensure_found_capacity_locked(state, 1)) {
        found_record_t *dst = &state->found_records[state->found_count++];
        uint64_t now = monotonic_millis();
        ttak_bigint_init_copy(&dst->seed, &params->record->seed, now);
        ttak_bigint_init_copy(&dst->max_value, &params->record->max_value, now);
        ttak_bigint_init_copy(&dst->final_value, &params->record->final_value, now);
        dst->steps = params->record->steps;
        dst->cycle_length = params->record->cycle_length;
        memcpy(dst->status, params->record->status, sizeof(dst->status));
        memcpy(dst->provenance, params->record->provenance, sizeof(dst->provenance));
        params->ok = true;
    }
    ttak_mutex_unlock(&state->lock);
}

static void ledger_owner_store_jump(void *ctx, void *args) {
    ledger_state_t *state = (ledger_state_t *)ctx;
    ledger_store_jump_args_t *params = (ledger_store_jump_args_t *)args;
    if (!state || !params || !params->record) return;
    ttak_mutex_lock(&state->lock);
    if (ledger_ensure_jump_capacity_locked(state, 1)) {
        jump_record_t *dst = &state->jump_records[state->jump_count++];
        uint64_t now = monotonic_millis();
        ttak_bigint_init_copy(&dst->seed, &params->record->seed, now);
        ttak_bigint_init_copy(&dst->preview_max, &params->record->preview_max, now);
        dst->preview_steps = params->record->preview_steps;
        dst->score = params->record->score;
        dst->overflow_pressure = params->record->overflow_pressure;
        params->ok = true;
    }
    ttak_mutex_unlock(&state->lock);
}

static void ledger_owner_store_track(void *ctx, void *args) {
    ledger_state_t *state = (ledger_state_t *)ctx;
    ledger_store_track_args_t *params = (ledger_store_track_args_t *)args;
    if (!state || !params || !params->record) return;
    ttak_mutex_lock(&state->lock);
    if (ledger_ensure_track_capacity_locked(state, 1)) {
        track_record_t *dst = &state->track_records[state->track_count++];
        uint64_t now = monotonic_millis();
        ttak_bigint_init_copy(&dst->seed, &params->record->seed, now);
        dst->steps = params->record->steps;
        dst->wall_time_ms = params->record->wall_time_ms;
        dst->wall_time_us = params->record->wall_time_us;
        dst->budget_ms = params->record->budget_ms;
        dst->max_step = params->record->max_step;
        dst->max_bits = params->record->max_bits;
        dst->max_dec_digits = params->record->max_dec_digits;
        dst->scout_score = params->record->scout_score;
        dst->priority = params->record->priority;
        memcpy(dst->ended, params->record->ended, sizeof(dst->ended));
        memcpy(dst->ended_by, params->record->ended_by, sizeof(dst->ended_by));
        memcpy(dst->max_hash, params->record->max_hash, sizeof(dst->max_hash));
        memcpy(dst->max_prefix, params->record->max_prefix, sizeof(dst->max_prefix));
        if (params->record->max_value_dec) {
            size_t len = strlen(params->record->max_value_dec);
            dst->max_value_dec = ttak_mem_alloc(len + 1, __TTAK_UNSAFE_MEM_FOREVER__, now);
            if (dst->max_value_dec) memcpy(dst->max_value_dec, params->record->max_value_dec, len + 1);
        } else dst->max_value_dec = NULL;
        params->ok = true;
    }
    ttak_mutex_unlock(&state->lock);
}

static void ledger_owner_persist_found(void *ctx, void *args) {
    (void)args;
    ledger_state_t *state = (ledger_state_t *)ctx;
    if (!state) return;
    ttak_mutex_lock(&state->lock);
    if (state->persisted_found_count >= state->found_count) {
        ttak_mutex_unlock(&state->lock);
        return;
    }
    FILE *fp = fopen(g_found_log_path, "a");
    if (!fp) {
        ttak_mutex_unlock(&state->lock);
        return;
    }
    uint64_t now = monotonic_millis();
    for (size_t i = state->persisted_found_count; i < state->found_count; ++i) {
        const found_record_t *rec = &state->found_records[i];
        char *s_seed = ttak_bigint_to_string(&rec->seed, now);
        char *s_max = ttak_bigint_to_string(&rec->max_value, now);
        char *s_final = ttak_bigint_to_string(&rec->final_value, now);
        fprintf(fp, "{\"seed\":\"%s\",\"steps\":%" PRIu64 ",\"max\":\"%s\",\"final\":\"%s\",\"cycle\":%u,\"status\":\"%s\",\"source\":\"%s\"}\n",
                s_seed ? s_seed : "0", rec->steps, s_max ? s_max : "0", s_final ? s_final : "0",
                rec->cycle_length, rec->status, rec->provenance);
        if (s_seed) ttak_mem_free(s_seed);
        if (s_max) ttak_mem_free(s_max);
        if (s_final) ttak_mem_free(s_final);
    }
    fclose(fp);
    state->persisted_found_count = state->found_count;
    ttak_mutex_unlock(&state->lock);
}

static void ledger_owner_persist_jump(void *ctx, void *args) {
    (void)args;
    ledger_state_t *state = (ledger_state_t *)ctx;
    if (!state) return;
    ttak_mutex_lock(&state->lock);
    if (state->persisted_jump_count >= state->jump_count) {
        ttak_mutex_unlock(&state->lock);
        return;
    }
    FILE *fp = fopen(g_jump_log_path, "a");
    if (!fp) {
        ttak_mutex_unlock(&state->lock);
        return;
    }
    uint64_t now = monotonic_millis();
    for (size_t i = state->persisted_jump_count; i < state->jump_count; ++i) {
        const jump_record_t *rec = &state->jump_records[i];
        char *s_seed = ttak_bigint_to_string(&rec->seed, now);
        char *s_max = ttak_bigint_to_string(&rec->preview_max, now);
        fprintf(fp, "{\"seed\":\"%s\",\"steps\":%" PRIu64 ",\"max\":\"%s\",\"score\":%.2f,\"overflow\":%.3f}\n",
                s_seed ? s_seed : "0", rec->preview_steps, s_max ? s_max : "0", rec->score, rec->overflow_pressure);
        if (s_seed) ttak_mem_free(s_seed);
        if (s_max) ttak_mem_free(s_max);
    }
    fclose(fp);
    state->persisted_jump_count = state->jump_count;
    ttak_mutex_unlock(&state->lock);
}

static void ledger_owner_persist_track(void *ctx, void *args) {
    (void)args;
    ledger_state_t *state = (ledger_state_t *)ctx;
    if (!state) return;
    ttak_mutex_lock(&state->lock);
    if (state->persisted_track_count >= state->track_count) {
        ttak_mutex_unlock(&state->lock);
        return;
    }
    FILE *fp = fopen(g_track_log_path, "a");
    if (!fp) {
        ttak_mutex_unlock(&state->lock);
        return;
    }
    uint64_t now = monotonic_millis();
    for (size_t i = state->persisted_track_count; i < state->track_count; ++i) {
        track_record_t *rec = &state->track_records[i];
        char *s_seed = ttak_bigint_to_string(&rec->seed, now);
        fprintf(fp, "{\"seed\":\"%s\",\"steps\":%" PRIu64 ",\"bits\":%u,\"digits\":%u,"
                "\"hash\":\"%s\",\"prefix\":\"%s\",\"ended\":\"%s\",\"ended_by\":\"%s\",\"wall_ms\":%" PRIu64 ",\"wall_us\":%" PRIu64 ","
                "\"budget_ms\":%" PRIu64 ",\"score\":%.2f,\"priority\":%u,\"max_step\":%u,\"max_value\":\"%s\"}\n",
                s_seed ? s_seed : "0", rec->steps, rec->max_bits, rec->max_dec_digits,
                rec->max_hash, rec->max_prefix, rec->ended, rec->ended_by, rec->wall_time_ms, rec->wall_time_us,
                rec->budget_ms, rec->scout_score, rec->priority, rec->max_step,
                rec->max_value_dec ? rec->max_value_dec : "unknown");
        if (s_seed) ttak_mem_free(s_seed);
        if (rec->max_value_dec) {
            ttak_mem_free(rec->max_value_dec);
            rec->max_value_dec = NULL;
        }
    }
    fclose(fp);
    state->persisted_track_count = state->track_count;
    ttak_mutex_unlock(&state->lock);
}

static void ledger_owner_mark_found_persisted(void *ctx, void *args) {
    (void)args;
    ledger_state_t *state = (ledger_state_t *)ctx;
    if (!state) return;
    ttak_mutex_lock(&state->lock);
    state->persisted_found_count = state->found_count;
    ttak_mutex_unlock(&state->lock);
}

static void ledger_owner_mark_jump_persisted(void *ctx, void *args) {
    (void)args;
    ledger_state_t *state = (ledger_state_t *)ctx;
    if (!state) return;
    ttak_mutex_lock(&state->lock);
    state->persisted_jump_count = state->jump_count;
    ttak_mutex_unlock(&state->lock);
}

static void ledger_owner_mark_track_persisted(void *ctx, void *args) {
    (void)args;
    ledger_state_t *state = (ledger_state_t *)ctx;
    if (!state) return;
    ttak_mutex_lock(&state->lock);
    state->persisted_track_count = state->track_count;
    ttak_mutex_unlock(&state->lock);
}

static bool ledger_store_found_record(const found_record_t *rec) {
    if (!g_ledger_owner || !rec) return false;
    ledger_store_found_args_t args = {.record = rec, .ok = false};
    ttak_owner_execute(g_ledger_owner, "store_found", LEDGER_RESOURCE_NAME, &args);
    return args.ok;
}

static bool ledger_store_jump_record(const jump_record_t *rec) {
    if (!g_ledger_owner || !rec) return false;
    ledger_store_jump_args_t args = {.record = rec, .ok = false};
    ttak_owner_execute(g_ledger_owner, "store_jump", LEDGER_RESOURCE_NAME, &args);
    return args.ok;
}

static bool ledger_store_track_record(const track_record_t *rec) {
    if (!g_ledger_owner || !rec) return false;
    ledger_store_track_args_t args = {.record = rec, .ok = false};
    ttak_owner_execute(g_ledger_owner, "store_track", LEDGER_RESOURCE_NAME, &args);
    return args.ok;
}

static void ledger_mark_found_persisted(void) {
    if (g_ledger_owner) ttak_owner_execute(g_ledger_owner, "mark_found_persisted", LEDGER_RESOURCE_NAME, NULL);
}

static void ledger_mark_jump_persisted(void) {
    if (g_ledger_owner) ttak_owner_execute(g_ledger_owner, "mark_jump_persisted", LEDGER_RESOURCE_NAME, NULL);
}

static void ledger_mark_track_persisted(void) {
    if (g_ledger_owner) ttak_owner_execute(g_ledger_owner, "mark_track_persisted", LEDGER_RESOURCE_NAME, NULL);
}

static bool ledger_init_owner(void) {
    if (ttak_mutex_init(&g_ledger_state.lock) != 0) {
        fprintf(stderr, "[ALIQUOT] Failed to init ledger mutex\n");
        return false;
    }
    g_ledger_owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    if (!g_ledger_owner) {
        fprintf(stderr, "[ALIQUOT] Failed to create ledger owner\n");
        return false;
    }
    if (!ttak_owner_register_resource(g_ledger_owner, LEDGER_RESOURCE_NAME, &g_ledger_state)) {
        fprintf(stderr, "[ALIQUOT] Failed to register ledger resource\n");
        ttak_owner_destroy(g_ledger_owner);
        g_ledger_owner = NULL;
        return false;
    }
    bool ok = true;
    ok &= ttak_owner_register_func(g_ledger_owner, "store_found", ledger_owner_store_found);
    ok &= ttak_owner_register_func(g_ledger_owner, "store_jump", ledger_owner_store_jump);
    ok &= ttak_owner_register_func(g_ledger_owner, "store_track", ledger_owner_store_track);
    ok &= ttak_owner_register_func(g_ledger_owner, "persist_found", ledger_owner_persist_found);
    ok &= ttak_owner_register_func(g_ledger_owner, "persist_jump", ledger_owner_persist_jump);
    ok &= ttak_owner_register_func(g_ledger_owner, "persist_track", ledger_owner_persist_track);
    ok &= ttak_owner_register_func(g_ledger_owner, "mark_found_persisted", ledger_owner_mark_found_persisted);
    ok &= ttak_owner_register_func(g_ledger_owner, "mark_jump_persisted", ledger_owner_mark_jump_persisted);
    ok &= ttak_owner_register_func(g_ledger_owner, "mark_track_persisted", ledger_owner_mark_track_persisted);
    if (!ok) {
        fprintf(stderr, "[ALIQUOT] Failed to register ledger owner funcs\n");
        ttak_owner_destroy(g_ledger_owner);
        g_ledger_owner = NULL;
        return false;
    }
    return true;
}

static void ledger_destroy_owner(void) {
    if (g_ledger_owner) {
        ttak_owner_destroy(g_ledger_owner);
        g_ledger_owner = NULL;
    }
    
    uint64_t now = monotonic_millis();
    for (size_t i = 0; i < g_ledger_state.found_count; ++i) {
        ttak_bigint_free(&g_ledger_state.found_records[i].seed, now);
        ttak_bigint_free(&g_ledger_state.found_records[i].max_value, now);
        ttak_bigint_free(&g_ledger_state.found_records[i].final_value, now);
    }
    if (g_ledger_state.found_records) ttak_mem_free(g_ledger_state.found_records);

    for (size_t i = 0; i < g_ledger_state.jump_count; ++i) {
        ttak_bigint_free(&g_ledger_state.jump_records[i].seed, now);
        ttak_bigint_free(&g_ledger_state.jump_records[i].preview_max, now);
    }
    if (g_ledger_state.jump_records) ttak_mem_free(g_ledger_state.jump_records);

    for (size_t i = 0; i < g_ledger_state.track_count; ++i) {
        ttak_bigint_free(&g_ledger_state.track_records[i].seed, now);
        if (g_ledger_state.track_records[i].max_value_dec)
            ttak_mem_free(g_ledger_state.track_records[i].max_value_dec);
    }
    if (g_ledger_state.track_records) ttak_mem_free(g_ledger_state.track_records);

    ttak_mutex_destroy(&g_ledger_state.lock);
}

static void append_found_record(const aliquot_outcome_t *out, const char *source) {
    if (!out) return;
    found_record_t rec = {0};
    uint64_t now = monotonic_millis();
    ttak_bigint_init_copy(&rec.seed, &out->seed, now);
    ttak_bigint_init_copy(&rec.max_value, &out->max_value, now);
    ttak_bigint_init_copy(&rec.final_value, &out->final_value, now);
    rec.steps = out->steps;
    rec.cycle_length = out->cycle_length;
    snprintf(rec.status, sizeof(rec.status), "%s", classify_outcome(out));
    if (source) snprintf(rec.provenance, sizeof(rec.provenance), "%s", source);
    else rec.provenance[0] = '\0';
    if (!ledger_store_found_record(&rec)) {
        ttak_bigint_free(&rec.seed, now);
        ttak_bigint_free(&rec.max_value, now);
        ttak_bigint_free(&rec.final_value, now);
        return;
    }
    char *s_seed = ttak_bigint_to_string(&rec.seed, now);
    printf("[ALIQUOT] seed=%s steps=%" PRIu64 " status=%s via %s\n",
            s_seed ? s_seed : "0", rec.steps, rec.status,
            rec.provenance[0] ? rec.provenance : "unknown");
    if (s_seed) ttak_mem_free(s_seed);
    
    ttak_bigint_free(&rec.seed, now);
    ttak_bigint_free(&rec.max_value, now);
    ttak_bigint_free(&rec.final_value, now);
    
    ttak_atomic_inc64(&g_total_sequences);
}

static void append_jump_record(const ttak_bigint_t *seed, uint64_t steps, const ttak_bigint_t *max_value, double score, double overflow_pressure) {
    jump_record_t rec = {0};
    uint64_t now = monotonic_millis();
    ttak_bigint_init_copy(&rec.seed, seed, now);
    ttak_bigint_init_copy(&rec.preview_max, max_value, now);
    rec.preview_steps = steps;
    rec.score = score;
    rec.overflow_pressure = overflow_pressure;
    
    if (!ledger_store_jump_record(&rec)) {
        ttak_bigint_free(&rec.seed, now);
        ttak_bigint_free(&rec.preview_max, now);
        return;
    }
    char *s_seed = ttak_bigint_to_string(&rec.seed, now);
    char *s_max = ttak_bigint_to_string(&rec.preview_max, now);
    printf("[SCOUT] seed=%s steps=%" PRIu64 " max=%s score=%.2f overflow=%.2f\n",
            s_seed ? s_seed : "0", steps, s_max ? s_max : "0", score, overflow_pressure);
    if (s_seed) ttak_mem_free(s_seed);
    if (s_max) ttak_mem_free(s_max);

    ttak_bigint_free(&rec.seed, now);
    ttak_bigint_free(&rec.preview_max, now);
}

static const char *track_end_reason(const aliquot_outcome_t *out) {
    if (!out) return "unknown";
    if (out->overflow) return "overflow";
    if (out->catalog_hit) return "catalog";
    if (out->perfect) return "perfect";
    if (out->amicable) return "amicable";
    if (out->entered_cycle) return "cycle";
    if (out->terminated) return "terminated";
    if (out->time_budget_hit) return "time-budget";
    if (out->hit_limit) return "step-limit";
    return "open";
}

static void format_track_end_detail(const aliquot_outcome_t *out, char *dest, size_t dest_cap) {
    if (!dest || dest_cap == 0) return;
    if (!out) { snprintf(dest, dest_cap, "unknown"); return; }
    if (out->overflow) { snprintf(dest, dest_cap, "overflow"); return; }
    if (out->catalog_hit) { snprintf(dest, dest_cap, "catalog_hit"); return; }
    if (out->time_budget_hit) { snprintf(dest, dest_cap, "time_budget"); return; }
    if (out->entered_cycle) {
        if (out->cycle_length > 0) snprintf(dest, dest_cap, "cycle_%u", out->cycle_length);
        else snprintf(dest, dest_cap, "cycle");
        return;
    }
    if (out->terminated) {
        char *s = ttak_bigint_to_string(&out->final_value, monotonic_millis());
        snprintf(dest, dest_cap, "reached_%s", s ? s : "0");
        if (s) ttak_mem_free(s);
        return;
    }
    if (out->hit_limit) { snprintf(dest, dest_cap, "step_limit"); return; }
    snprintf(dest, dest_cap, "open");
}

static bool aliquot_outcome_set_decimal_from_bigint(aliquot_outcome_t *out, const ttak_bigint_t *value, uint64_t now) {
    if (!out || !value) return false;
    char *digits = ttak_bigint_to_string(value, now);
    if (!digits) return false;
    if (out->max_value_dec) ttak_mem_free(out->max_value_dec);
    out->max_value_dec = digits;
    out->max_dec_digits = (uint32_t)strlen(digits);
    return true;
}

static void aliquot_outcome_cleanup(aliquot_outcome_t *out) {
    if (!out) return;
    uint64_t now = monotonic_millis();
    ttak_bigint_free(&out->seed, now);
    ttak_bigint_free(&out->max_value, now);
    ttak_bigint_free(&out->final_value, now);
    if (out->max_value_dec) { ttak_mem_free(out->max_value_dec); out->max_value_dec = NULL; }
}

static void capture_track_metrics(aliquot_outcome_t *out, const aliquot_job_t *job, uint64_t budget_ms, track_record_t *rec) {
    if (!out || !rec) return;
    memset(rec, 0, sizeof(*rec));
    uint64_t now = monotonic_millis();
    ttak_bigint_init_copy(&rec->seed, &out->seed, now);
    rec->steps = out->steps;
    rec->wall_time_ms = out->wall_time_ms;
    rec->wall_time_us = out->wall_time_us;
    rec->budget_ms = budget_ms;
    rec->scout_score = job ? job->scout_score : 0.0;
    rec->priority = job ? job->priority : 0;
    rec->max_step = out->max_step_index;
    snprintf(rec->ended, sizeof(rec->ended), "%s", track_end_reason(out));
    format_track_end_detail(out, rec->ended_by, sizeof(rec->ended_by));

    if (out->max_value_dec) {
        rec->max_value_dec = out->max_value_dec;
        out->max_value_dec = NULL;
        rec->max_dec_digits = (uint32_t)strlen(rec->max_value_dec);
    }

    rec->max_bits = (uint32_t)ttak_bigint_get_bit_length(&out->max_value);
    snprintf(rec->max_hash, sizeof(rec->max_hash), "%s", out->max_hash);
    snprintf(rec->max_prefix, sizeof(rec->max_prefix), "%s", out->max_prefix);
}

static void append_track_record(aliquot_outcome_t *out, const aliquot_job_t *job, uint64_t budget_ms) {
    if (!out) return;
    track_record_t rec = {0};
    capture_track_metrics(out, job, budget_ms, &rec);
    if (!ledger_store_track_record(&rec)) {
        if (rec.max_value_dec) ttak_mem_free(rec.max_value_dec);
        ttak_bigint_free(&rec.seed, monotonic_millis());
        return;
    }
    char *s_seed = ttak_bigint_to_string(&rec.seed, monotonic_millis());
    printf("[TRACK] seed=%s bits=%u ended_by=%s\n",
           s_seed ? s_seed : "0", rec.max_bits, rec.ended_by);
    if (s_seed) ttak_mem_free(s_seed);
    ttak_bigint_free(&rec.seed, monotonic_millis());
}

static uint64_t determine_time_budget(const aliquot_job_t *job) {
    if (!job) return TRACK_FAST_BUDGET_MS;
    if (job->priority >= 3 || job->preview_overflow || job->scout_score >= SCOUT_SCORE_GATE * 1.5)
        return TRACK_DEEP_BUDGET_MS;
    return TRACK_FAST_BUDGET_MS;
}

static void rehydrate_found_record(const found_record_t *rec) {
    if (!rec) return;
    ledger_store_found_record(rec);
}

static void rehydrate_jump_record(const jump_record_t *rec) {
    if (!rec) return;
    ledger_store_jump_record(rec);
}

static void rehydrate_track_record(const track_record_t *rec) {
    if (!rec) return;
    ledger_store_track_record(rec);
}

#if defined(ENABLE_CUDA) || defined(ENABLE_ROCM) || defined(ENABLE_OPENCL)
static void tracker_gpu_seed_rate_record(uint64_t now_ms) {
    uint64_t prev_ms = g_gpu_rate_last_sync_ms;
    uint64_t prev_sequences = g_gpu_rate_last_sequences;
    uint64_t current_sequences = ttak_atomic_read64(&g_total_sequences);
    g_gpu_rate_last_sync_ms = now_ms;
    g_gpu_rate_last_sequences = current_sequences;
    if (prev_ms == 0 || now_ms <= prev_ms || current_sequences < prev_sequences) {
        return;
    }
    double secs = (double)(now_ms - prev_ms) / 1000.0;
    if (secs <= 0.0) {
        return;
    }
    double rate = (double)(current_sequences - prev_sequences) / secs;
    if (rate < 0.0) {
        rate = 0.0;
    }
    uint64_t bits;
    memcpy(&bits, &rate, sizeof(bits));
    ttak_atomic_write64(&g_gpu_rate_bits, bits);
}

static double tracker_gpu_seed_rate_read(void) {
    uint64_t bits = ttak_atomic_read64(&g_gpu_rate_bits);
    double rate;
    memcpy(&rate, &bits, sizeof(rate));
    return rate;
}
#endif

static void persist_found_records(void) {
    if (g_ledger_owner) ttak_owner_execute(g_ledger_owner, "persist_found", LEDGER_RESOURCE_NAME, NULL);
}

static void persist_jump_records(void) {
    if (g_ledger_owner) ttak_owner_execute(g_ledger_owner, "persist_jump", LEDGER_RESOURCE_NAME, NULL);
}

static void persist_track_records(void) {
    if (g_ledger_owner) ttak_owner_execute(g_ledger_owner, "persist_track", LEDGER_RESOURCE_NAME, NULL);
}

static void persist_queue_state(void) {
    ttak_bigint_t pending[JOB_QUEUE_CAP];
    size_t count = pending_queue_snapshot(pending, JOB_QUEUE_CAP);
    FILE *fp = fopen(g_queue_state_path, "w");
    if (!fp) {
        for (size_t i = 0; i < count; ++i) ttak_bigint_free(&pending[i], monotonic_millis());
        return;
    }
    uint64_t now = monotonic_millis();
    fprintf(fp, "{\"pending\":[");
    for (size_t i = 0; i < count; ++i) {
        char *s = ttak_bigint_to_string(&pending[i], now);
        fprintf(fp, "%s\"%s\"", (i == 0) ? "" : ",", s ? s : "0");
        if (s) ttak_mem_free(s);
        ttak_bigint_free(&pending[i], now);
    }
    fprintf(fp, "],\"ts\":%" PRIu64 "}\n", now);
    fclose(fp);
}

static void flush_ledgers(void) {
    ttak_mutex_lock(&g_disk_lock);
    persist_found_records(); persist_jump_records(); persist_track_records(); persist_queue_state();
    uint64_t now = monotonic_millis();
    ttak_atomic_write64(&g_last_persist_ms, now);
#if defined(ENABLE_CUDA) || defined(ENABLE_ROCM) || defined(ENABLE_OPENCL)
    tracker_gpu_seed_rate_record(now);
#endif
    ttak_mutex_unlock(&g_disk_lock);
}

static void maybe_flush_ledgers(void) {
    uint64_t now = monotonic_millis();
    if (now - ttak_atomic_read64(&g_last_persist_ms) >= FLUSH_INTERVAL_MS) flush_ledgers();
}

static bool ttak_bigint_init_from_string(ttak_bigint_t *bi, const char *s, uint64_t now) {
    if (!bi || !s) return false;
    ttak_bigint_init(bi, now);
    while (*s) {
        if (isdigit((unsigned char)*s)) {
            if (!ttak_bigint_mul_u64(bi, bi, 10, now)) return false;
            if (!ttak_bigint_add_u64(bi, bi, *s - '0', now)) return false;
        }
        s++;
    }
    return true;
}

static bool json_extract_string(const char *json, const char *key, char *dest, size_t dest_cap) {
    char needle[64]; snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char *start = strstr(json, needle);
    if (!start) return false;
    start += strlen(needle);
    const char *end = strchr(start, '"');
    if (!end) return false;
    size_t len = (size_t)(end - start);
    if (len >= dest_cap) len = dest_cap - 1;
    memcpy(dest, start, len); dest[len] = '\0';
    return true;
}

static bool json_extract_uint64(const char *json, const char *key, uint64_t *val) {
    char needle[64]; snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *start = strstr(json, needle);
    if (!start) return false;
    start += strlen(needle);
    char *endptr = NULL; *val = strtoull(start, &endptr, 10);
    return endptr != start;
}

static bool json_extract_uint32(const char *json, const char *key, uint32_t *val) {
    uint64_t tmp; if (!json_extract_uint64(json, key, &tmp)) return false;
    *val = (uint32_t)tmp; return true;
}

static bool json_extract_double(const char *json, const char *key, double *val) {
    char needle[64]; snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *start = strstr(json, needle);
    if (!start) return false;
    start += strlen(needle);
    char *endptr = NULL; *val = strtod(start, &endptr);
    return endptr != start;
}

static void load_found_records(void) {
    FILE *fp = fopen(g_found_log_path, "r");
    if (!fp) return;
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        found_record_t rec = {0};
        char s_seed[512], s_max[512], s_final[512];
        if (json_extract_string(line, "seed", s_seed, sizeof(s_seed)) &&
            json_extract_uint64(line, "steps", &rec.steps) &&
            json_extract_string(line, "max", s_max, sizeof(s_max)) &&
            json_extract_string(line, "final", s_final, sizeof(s_final)) &&
            json_extract_uint32(line, "cycle", &rec.cycle_length) &&
            json_extract_string(line, "status", rec.status, sizeof(rec.status)) &&
            json_extract_string(line, "source", rec.provenance, sizeof(rec.provenance))) {
            
            uint64_t now = monotonic_millis();
            ttak_bigint_init_from_string(&rec.seed, s_seed, now);
            ttak_bigint_init_from_string(&rec.max_value, s_max, now);
            ttak_bigint_init_from_string(&rec.final_value, s_final, now);
            
            rehydrate_found_record(&rec);
            seed_registry_mark(&rec.seed);
            
            ttak_bigint_free(&rec.seed, now);
            ttak_bigint_free(&rec.max_value, now);
            ttak_bigint_free(&rec.final_value, now);
        }
    }
    fclose(fp);
    ledger_mark_found_persisted();
}

static void load_jump_records(void) {
    FILE *fp = fopen(g_jump_log_path, "r");
    if (!fp) return;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        jump_record_t rec = {0};
        char s_seed[512], s_max[512];
        if (json_extract_string(line, "seed", s_seed, sizeof(s_seed)) &&
            json_extract_uint64(line, "steps", &rec.preview_steps) &&
            json_extract_string(line, "max", s_max, sizeof(s_max)) &&
            json_extract_double(line, "score", &rec.score) &&
            json_extract_double(line, "overflow", &rec.overflow_pressure)) {
            
            uint64_t now = monotonic_millis();
            ttak_bigint_init_from_string(&rec.seed, s_seed, now);
            ttak_bigint_init_from_string(&rec.preview_max, s_max, now);
            
            rehydrate_jump_record(&rec);
            
            ttak_bigint_free(&rec.seed, now);
            ttak_bigint_free(&rec.preview_max, now);
        }
    }
    fclose(fp);
    ledger_mark_jump_persisted();
}

static void load_track_records(void) {
    FILE *fp = fopen(g_track_log_path, "r");
    if (!fp) return;
    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        track_record_t rec = {0};
        char s_seed[512];
        if (json_extract_string(line, "seed", s_seed, sizeof(s_seed)) &&
            json_extract_uint64(line, "steps", &rec.steps) &&
            json_extract_uint32(line, "bits", &rec.max_bits) &&
            json_extract_uint32(line, "digits", &rec.max_dec_digits) &&
            json_extract_string(line, "hash", rec.max_hash, sizeof(rec.max_hash)) &&
            json_extract_string(line, "prefix", rec.max_prefix, sizeof(rec.max_prefix)) &&
            json_extract_string(line, "ended", rec.ended, sizeof(rec.ended)) &&
            json_extract_string(line, "ended_by", rec.ended_by, sizeof(rec.ended_by)) &&
            json_extract_uint64(line, "wall_ms", &rec.wall_time_ms) &&
            json_extract_uint64(line, "wall_us", &rec.wall_time_us) &&
            json_extract_uint64(line, "budget_ms", &rec.budget_ms) &&
            json_extract_double(line, "score", &rec.scout_score) &&
            json_extract_uint32(line, "priority", &rec.priority) &&
            json_extract_uint32(line, "max_step", &rec.max_step)) {
            
            uint64_t now = monotonic_millis();
            ttak_bigint_init_from_string(&rec.seed, s_seed, now);
            
            char s_max[8192];
            if (json_extract_string(line, "max_value", s_max, sizeof(s_max))) {
                rec.max_value_dec = ttak_mem_alloc(strlen(s_max) + 1, __TTAK_UNSAFE_MEM_FOREVER__, now);
                if (rec.max_value_dec) strcpy(rec.max_value_dec, s_max);
            }

            rehydrate_track_record(&rec);
            
            ttak_bigint_free(&rec.seed, now);
            if (rec.max_value_dec) { ttak_mem_free(rec.max_value_dec); rec.max_value_dec = NULL; }
        }
    }
    fclose(fp);
    ledger_mark_track_persisted();
}

static bool enqueue_job(aliquot_job_t *job, const char *source_tag) {
    if (!job || !g_thread_pool) return false;
    if (!pending_queue_add(&job->seed)) return false;
    uint64_t now = monotonic_millis();
    ttak_future_t *future = ttak_thread_pool_submit_task(g_thread_pool, worker_process_job_wrapper, job, job->priority, now);
    if (!future) { pending_queue_remove(&job->seed); return false; }
    return true;
}

static void load_queue_checkpoint(void) {
    if (!g_thread_pool) return;
    FILE *fp = fopen(g_queue_state_path, "r");
    if (!fp) return;
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); if (sz <= 0) { fclose(fp); return; }
    rewind(fp); uint64_t now = monotonic_millis();
    char *buf = ttak_mem_alloc((size_t)sz + 1, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, (size_t)sz, fp); buf[sz] = '\0'; fclose(fp);
    
    const char *p = strstr(buf, "\"pending\":[");
    if (p) {
        p += 11;
        while (*p && *p != ']') {
            if (*p == '"') {
                p++;
                const char *start = p;
                while (*p && *p != '"') p++;
                if (*p == '"') {
                    char s_seed[512];
                    size_t len = (size_t)(p - start);
                    if (len < sizeof(s_seed)) {
                        memcpy(s_seed, start, len); s_seed[len] = '\0';
                        ttak_bigint_t seed;
                        if (ttak_bigint_init_from_string(&seed, s_seed, now)) {
                            if (seed_registry_try_add(&seed)) {
                                aliquot_job_t *job = ttak_mem_alloc(sizeof(aliquot_job_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
                                if (job) {
                                    memset(job, 0, sizeof(*job));
                                    ttak_bigint_init_copy(&job->seed, &seed, now);
                                    job->priority = 1;
                                    snprintf(job->provenance, sizeof(job->provenance), "checkpoint");
                                    if (!enqueue_job(job, "checkpoint")) {
                                        ttak_bigint_free(&job->seed, now);
                                        ttak_mem_free(job);
                                    }
                                }
                            }
                            ttak_bigint_free(&seed, now);
                        }
                    }
                    p++;
                }
            } else p++;
        }
    }
    ttak_mem_free(buf);
}

static void *worker_process_job_wrapper(void *arg) {
    aliquot_job_t *job = (aliquot_job_t *)arg;
    pending_queue_remove(&job->seed);
    process_job(job);
    ttak_mem_free(job);
    return NULL;
}

static void process_job(const aliquot_job_t *job) {
    if (!job) return;
    aliquot_outcome_t outcome;
    uint64_t now = monotonic_millis();
    ttak_bigint_init_copy(&outcome.seed, &job->seed, now);
    ttak_bigint_init(&outcome.max_value, now);
    ttak_bigint_init(&outcome.final_value, now);
    outcome.max_bits = 0; outcome.max_step_index = 0; outcome.max_value_dec = NULL;
    outcome.wall_time_ms = 0; outcome.wall_time_us = 0;
    outcome.steps = 0; outcome.cycle_length = 0;
    outcome.terminated = false; outcome.entered_cycle = false;
    outcome.amicable = false; outcome.perfect = false; outcome.overflow = false;
    outcome.hit_limit = false; outcome.time_budget_hit = false; outcome.catalog_hit = false;
    snprintf(outcome.max_hash, sizeof(outcome.max_hash), "0"); snprintf(outcome.max_prefix, sizeof(outcome.max_prefix), "0");

    uint64_t budget_ms = determine_time_budget(job);
    uint32_t max_steps = (job->priority >= 3) ? 0 : LONG_RUN_MAX_STEPS;
    run_aliquot_sequence(&job->seed, max_steps, budget_ms, &outcome);

    if (outcome.max_bits > 64 && outcome.hit_limit) {
        aliquot_job_t *retry = ttak_mem_alloc(sizeof(aliquot_job_t), __TTAK_UNSAFE_MEM_FOREVER__, monotonic_millis());
        if (retry) {
            memset(retry, 0, sizeof(*retry));
            ttak_bigint_init_copy(&retry->seed, &job->seed, monotonic_millis());
            retry->priority = 10;
            retry->scout_score = job->scout_score; snprintf(retry->provenance, sizeof(retry->provenance), "retry-big");
            if (enqueue_job(retry, "retry-limit")) {
                append_track_record(&outcome, job, budget_ms); maybe_flush_ledgers(); aliquot_outcome_cleanup(&outcome);
                return;
            } else {
                ttak_bigint_free(&retry->seed, monotonic_millis());
                ttak_mem_free(retry);
            }
        }
    }
    append_found_record(&outcome, job->provenance);
    append_track_record(&outcome, job, budget_ms);
    maybe_flush_ledgers();
    aliquot_outcome_cleanup(&outcome);
}

static void *scout_main(void *arg) {
    (void)arg;
    while (!ttak_atomic_read64(&shutdown_requested)) {
        // Keep scouting as long as there's capacity in the pending queue
        // to submit more jobs. This helps ensure the worker pool stays fed.
        if (pending_queue_depth() >= JOB_QUEUE_CAP - 8) { 
            responsive_sleep(SCOUT_SLEEP_MS); 
            continue; 
        }
        
        ttak_bigint_t seed;
        uint64_t now = monotonic_millis();
        uint64_t rand_seed_val = random_seed_between(SCOUT_MIN_SEED, SCOUT_MAX_SEED);
        ttak_bigint_init_u64(&seed, rand_seed_val, now);

        if (!seed_registry_try_add(&seed)) {
            ttak_bigint_free(&seed, now);
            responsive_sleep(SCOUT_SLEEP_MS / 4); // Shorter sleep if seed already exists
            continue;
        }
        scan_result_t sr;
        if (!frontier_accept_seed(&seed, &sr)) { 
            ttak_bigint_free(&seed, now);
            responsive_sleep(SCOUT_SLEEP_MS / 4); // Shorter sleep if seed not accepted
            continue; 
        }
        
        aliquot_outcome_t probe;
        ttak_bigint_init_copy(&probe.seed, &seed, now);
        ttak_bigint_init(&probe.max_value, now);
        ttak_bigint_init(&probe.final_value, now);
        
        run_aliquot_sequence(&seed, SCOUT_PREVIEW_STEPS, 0, &probe);
        
        double score; double op = compute_overflow_pressure(&probe);
        if (looks_long(&probe, &score)) {
            append_jump_record(&seed, probe.steps, &probe.max_value, score, op);
            aliquot_job_t *job = ttak_mem_alloc(sizeof(aliquot_job_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
            if (job) {
                memset(job, 0, sizeof(*job));
                ttak_bigint_init_copy(&job->seed, &seed, now);
                job->priority = (probe.overflow || op >= 45.0) ? 3 : 2;
                job->preview_steps = probe.steps;
                ttak_bigint_init_copy(&job->preview_max, &probe.max_value, now);
                job->preview_overflow = probe.overflow || (op >= 45.0);
                job->scout_score = score;
                snprintf(job->provenance, sizeof(job->provenance), "scout");
                if (!enqueue_job(job, "scout")) {
                    ttak_bigint_free(&job->seed, now);
                    ttak_mem_free(job);
                } else maybe_flush_ledgers();
            }
        }
        aliquot_outcome_cleanup(&probe);
        ttak_bigint_free(&seed, now);
        responsive_sleep(SCOUT_SLEEP_MS);
    }
    return NULL;
}

int main(void) {
    printf("[ALIQUOT] Booting aliquot tracker...\n");
    seed_rng();
    configure_probe_quantum();
    configure_state_paths(); ensure_state_dir(); init_catalog_filters();
    printf("[ALIQUOT] Checkpoints at %s\n", g_state_dir);
    if (!ledger_init_owner()) return 1;
    struct sigaction sa = {0}; sa.sa_handler = handle_signal; sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);
    ttak_atomic_write64(&g_last_persist_ms, monotonic_millis());
    load_found_records(); load_jump_records(); load_track_records();

    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus < 1) {
        cpus = 1;
    }
    if (cpus > MAX_WORKERS) {
        cpus = MAX_WORKERS;
    }
    g_thread_pool = ttak_thread_pool_create((size_t)cpus, 0, monotonic_millis());
    if (!g_thread_pool) return 1;

    load_queue_checkpoint();
    
    int num_scouts = (cpus > 4) ? 2 : 1;
    pthread_t scout_threads[num_scouts];
    for (int i = 0; i < num_scouts; i++) {
        pthread_create(&scout_threads[i], NULL, scout_main, NULL);
    }

    /* SELF-SEEDING BOOTSTRAP: Ensure we have work immediately */
    if (pending_queue_depth() == 0) {
        printf("[ALIQUOT] Warm-up: Seeding initial jobs manually...\n");
        for (int i = 0; i < (int)cpus; i++) {
            uint64_t now = monotonic_millis();
            uint64_t rand_seed_val = random_seed_between(SCOUT_MIN_SEED, SCOUT_MAX_SEED);
            ttak_bigint_t seed;
            ttak_bigint_init_u64(&seed, rand_seed_val, now);

            if (seed_registry_try_add(&seed)) {
                aliquot_job_t *job = ttak_mem_alloc(sizeof(aliquot_job_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
                if (job) {
                    memset(job, 0, sizeof(*job));
                    ttak_bigint_init_copy(&job->seed, &seed, now);
                    job->priority = 1;
                    snprintf(job->provenance, sizeof(job->provenance), "warmup");
                    if (!enqueue_job(job, "warmup")) {
                        ttak_bigint_free(&job->seed, now);
                        ttak_mem_free(job);
                    }
                }
            }
            ttak_bigint_free(&seed, now);
        }
    }

    uint64_t status_last_ms = monotonic_millis();
    uint64_t status_last_probes = ttak_atomic_read64(&g_total_probes);
    double status_rate = 0.0;

    while (!ttak_atomic_read64(&shutdown_requested)) {
        size_t qd = pending_queue_depth();

        /* ACTIVE SCHEDULING: Hunt for seeds if queue is low */
        // Actively try to fill the queue up to a certain threshold to keep workers busy.
        // The cpus variable determines how many parallel jobs the thread pool can handle.
        // We want to keep at least 'cpus' number of jobs in queue.
        while (qd < (size_t)cpus * 2 && qd < JOB_QUEUE_CAP) {
            uint64_t now = monotonic_millis();
            uint64_t rand_seed_val = random_seed_between(SCOUT_MIN_SEED, SCOUT_MAX_SEED);
            ttak_bigint_t seed;
            ttak_bigint_init_u64(&seed, rand_seed_val, now);

            if (seed_registry_try_add(&seed)) {
                aliquot_job_t *job = ttak_mem_alloc(sizeof(aliquot_job_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
                if (job) {
                    memset(job, 0, sizeof(*job));
                    ttak_bigint_init_copy(&job->seed, &seed, now);
                    job->priority = 1;
                    snprintf(job->provenance, sizeof(job->provenance), "main_hunt");
                    if (!enqueue_job(job, "main_hunt")) {
                        ttak_bigint_free(&job->seed, now);
                        ttak_mem_free(job);
                    } else {
                        qd++; // Job successfully enqueued, increment queue depth locally
                    }
                }
            }
            ttak_bigint_free(&seed, now);
        }

        responsive_sleep(SCOUT_SLEEP_MS / 4); // Reduce sleep to be more responsive
        maybe_flush_ledgers();

        uint64_t now_ms = monotonic_millis();
        uint64_t completed = ttak_atomic_read64(&g_total_sequences);
        uint64_t probes_now = ttak_atomic_read64(&g_total_probes);
        uint64_t elapsed_ms = now_ms - status_last_ms;
        if (elapsed_ms >= 1000) {
            double secs = (double)elapsed_ms / 1000.0;
            if (secs > 0.0) {
                status_rate = (double)(probes_now - status_last_probes) / secs;
            }
            status_last_ms = now_ms;
            status_last_probes = probes_now;
        }
        printf(
#if defined(ENABLE_CUDA) || defined(ENABLE_ROCM) || defined(ENABLE_OPENCL)
               "[ALIQUOT] queue=%zu completed=%" PRIu64 " probes=%" PRIu64 " rate=%.2f probes/sec seed_rate=%.2f seeds/sec\n",
               qd, completed, probes_now, status_rate, tracker_gpu_seed_rate_read());
#else
               "[ALIQUOT] queue=%zu completed=%" PRIu64 " probes=%" PRIu64 " rate=%.2f probes/sec\n",
               qd, completed, probes_now, status_rate);
#endif
    }

    printf("[ALIQUOT] Shutdown requested. Waiting for threads to exit...\n");
    for (int i = 0; i < num_scouts; i++) {
        pthread_join(scout_threads[i], NULL);
    }
    ttak_thread_pool_destroy(g_thread_pool);
    flush_ledgers();
    ledger_destroy_owner();
    printf("[ALIQUOT] Shutdown complete.\n");
    return 0;
}
