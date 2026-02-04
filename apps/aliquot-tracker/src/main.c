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
#define SCOUT_SLEEP_MS      200
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
    uint64_t seed;
    uint64_t steps;
    uint64_t max_value;
    uint64_t final_value;
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
    uint64_t seed;
    uint64_t steps;
    uint64_t max_value;
    uint64_t final_value;
    uint32_t cycle_length;
    char status[24];
    char provenance[16];
} found_record_t;

typedef struct {
    uint64_t seed;
    uint64_t preview_steps;
    uint64_t preview_max;
    double score;
    double overflow_pressure;
} jump_record_t;

typedef enum {
    SCAN_END_CATALOG = 0,
    SCAN_END_OVERFLOW,
    SCAN_END_TIMECAP
} scan_end_reason_t;

typedef struct {
    uint64_t seed;
    uint64_t steps;
    uint64_t max_value;
    scan_end_reason_t ended_by;
} scan_result_t;

typedef struct {
    uint64_t modulus;
    uint64_t remainder;
} catalog_mod_rule_t;

typedef struct {
    uint64_t seed;
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
    uint64_t seed;
    char provenance[16];
    uint32_t priority;
    double scout_score;
    uint64_t preview_steps;
    uint64_t preview_max;
    bool preview_overflow;
} aliquot_job_t;

typedef struct seed_entry {
    uint64_t seed;
    struct seed_entry *next;
} seed_entry_t;

typedef struct {
    uint64_t seeds[JOB_QUEUE_CAP];
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
static bool aliquot_outcome_set_decimal_from_u64(aliquot_outcome_t *out, uint64_t value);
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

static ttak_thread_pool_t *g_thread_pool;

static double compute_overflow_pressure(const aliquot_outcome_t *out);
static void *worker_process_job_wrapper(void *arg);
static void process_job(const aliquot_job_t *job);
static bool enqueue_job(aliquot_job_t *job, const char *source_tag);
static uint32_t bit_length_u64(uint64_t value);
static bool ledger_init_owner(void);
static void ledger_destroy_owner(void);
static bool ledger_store_found_record(const found_record_t *rec);
static bool ledger_store_jump_record(const jump_record_t *rec);
static bool ledger_store_track_record(const track_record_t *rec);
static void ledger_mark_found_persisted(void);
static void ledger_mark_jump_persisted(void);
static void ledger_mark_track_persisted(void);

static bool pending_queue_add(uint64_t seed) {
    ttak_mutex_lock(&g_pending_lock);
    if (g_pending_queue.count >= JOB_QUEUE_CAP) {
        ttak_mutex_unlock(&g_pending_lock);
        return false;
    }
    g_pending_queue.seeds[g_pending_queue.count++] = seed;
    ttak_mutex_unlock(&g_pending_lock);
    return true;
}

static void pending_queue_remove(uint64_t seed) {
    ttak_mutex_lock(&g_pending_lock);
    for (size_t i = 0; i < g_pending_queue.count; ++i) {
        if (g_pending_queue.seeds[i] == seed) {
            g_pending_queue.seeds[i] = g_pending_queue.seeds[g_pending_queue.count - 1];
            g_pending_queue.count--;
            break;
        }
    }
    ttak_mutex_unlock(&g_pending_lock);
}

static size_t pending_queue_snapshot(uint64_t *dest, size_t cap) {
    if (!dest || cap == 0) return 0;
    ttak_mutex_lock(&g_pending_lock);
    size_t count = g_pending_queue.count;
    if (count > cap) count = cap;
    memcpy(dest, g_pending_queue.seeds, count * sizeof(uint64_t));
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

static void log_big_sum_failure(uint64_t seed, uint32_t steps, uint64_t current_value_u64,
                                const ttak_bigint_t *current_value, const char *stage) {
    if (!current_value) return;
    char hash[65];
    char prefix[TRACK_PREFIX_DIGITS + 1];
    ttak_bigint_to_hex_hash(current_value, hash);
    ttak_bigint_format_prefix(current_value, prefix, sizeof(prefix));
    uint32_t bits = ttak_bigint_get_bit_length(current_value);
    const char *reason = ttak_sum_proper_divisors_big_error_name(
        ttak_sum_proper_divisors_big_last_error());
    fprintf(stderr,
            "[ALIQUOT][BIGSUM] stage=%s seed=%" PRIu64 " step=%u bits=%u current=%" PRIu64
            " reason=%s prefix=%s hash=%s\n",
            stage ? stage : "unknown", seed, steps, bits, current_value_u64,
            reason ? reason : "unknown", prefix, hash);
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

static uint32_t bit_length_u64(uint64_t value) {
    if (value == 0) return 0;
#if defined(__GNUC__) || defined(__clang__)
    return 64U - (uint32_t)__builtin_clzll(value);
#else
    uint32_t bits = 0;
    while (value) {
        bits++;
        value >>= 1;
    }
    return bits;
#endif
}

static void ensure_state_dir(void) {
    struct stat st;
    if (stat(g_state_dir, &st) == 0 && S_ISDIR(st.st_mode)) return;
    if (mkdir(g_state_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "[ALIQUOT] Failed to create %s: %s\n", g_state_dir, strerror(errno));
    }
}

static bool sum_proper_divisors_u64_checked(uint64_t n, uint64_t *out) {
    if (!out) return false;
    return ttak_sum_proper_divisors_u64(n, out);
}

static void history_init(history_table_t *t) {
    memset(t, 0, sizeof(*t));
}

static void history_destroy(history_table_t *t) {
    for (size_t i = 0; i < HISTORY_BUCKETS; ++i) {
        history_entry_t *node = t->buckets[i];
        while (node) {
            history_entry_t *next = node->next;
            ttak_mem_free(node);
            node = next;
        }
        t->buckets[i] = NULL;
    }
}

static bool history_contains(history_table_t *t, uint64_t value, uint32_t *step_out) {
    size_t idx = value % HISTORY_BUCKETS;
    history_entry_t *node = t->buckets[idx];
    while (node) {
        if (node->value == value) {
            if (step_out) *step_out = node->step;
            return true;
        }
        node = node->next;
    }
    return false;
}

static void history_insert(history_table_t *t, uint64_t value, uint32_t step) {
    size_t idx = value % HISTORY_BUCKETS;
    uint64_t now = monotonic_millis();
    history_entry_t *node = ttak_mem_alloc(sizeof(*node), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!node) return;
    node->value = value;
    node->step = step;
    node->next = t->buckets[idx];
    t->buckets[idx] = node;
}

static bool seed_registry_try_add(uint64_t seed) {
    size_t idx = seed % SEED_REGISTRY_BUCKETS;
    ttak_mutex_lock(&g_seed_lock);
    seed_entry_t *node = g_seed_buckets[idx];
    while (node) {
        if (node->seed == seed) {
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
    node->seed = seed;
    node->next = g_seed_buckets[idx];
    g_seed_buckets[idx] = node;
    ttak_mutex_unlock(&g_seed_lock);
    return true;
}

static void seed_registry_mark(uint64_t seed) {
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

static bool is_catalog_value(uint64_t value) {
    for (size_t i = 0; i < g_catalog_exact_count; ++i) {
        if (g_catalog_exact[i] == value) return true;
    }
    for (size_t i = 0; i < g_catalog_mod_rule_count; ++i) {
        const catalog_mod_rule_t *rule = &g_catalog_mod_rules[i];
        if (rule->modulus && value % rule->modulus == rule->remainder) {
            return true;
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

static bool frontier_accept_seed(uint64_t seed, scan_result_t *result) {
    if (result) memset(result, 0, sizeof(*result));
    if (result) result->seed = seed;
    if (is_catalog_value(seed)) {
        if (result) result->ended_by = SCAN_END_CATALOG;
        return false;
    }
    uint64_t start_ms = monotonic_millis();
    uint64_t current = seed;
    uint64_t max_value = seed;
    uint32_t steps = 0;
    while (steps < SCAN_STEP_CAP) {
        if (ttak_atomic_read64(&shutdown_requested)) break;
        if (SCAN_TIMECAP_MS > 0) {
            uint64_t now = monotonic_millis();
            if (now - start_ms >= (uint64_t)SCAN_TIMECAP_MS) break;
        }
        uint64_t next = 0;
        bool ok = sum_proper_divisors_u64_checked(current, &next);
        ttak_atomic_inc64(&g_total_probes);
        steps++;

        if (!ok) {
            if (result) {
                result->ended_by = SCAN_END_OVERFLOW;
                result->steps = steps;
                result->max_value = max_value;
            }
            return true;
        }

        if (next > max_value) max_value = next;

        if (is_catalog_value(next)) {
            if (result) {
                result->ended_by = SCAN_END_CATALOG;
                result->steps = steps;
                result->max_value = max_value;
            }
            return false;
        }
        current = next;
    }
    if (result) {
        result->ended_by = SCAN_END_TIMECAP;
        result->steps = steps;
        result->max_value = max_value;
    }
    return true;
}

static bool sum_proper_divisors_big(const ttak_bigint_t *n, ttak_bigint_t *result, uint64_t now) {
    return ttak_sum_proper_divisors_big(n, result, now);
}

static void run_aliquot_sequence_big(ttak_bigint_t *start_val, uint32_t start_step, uint32_t max_steps, uint64_t time_budget_ms, aliquot_outcome_t *out, uint64_t start_ms) {
    history_big_table_t hist;
    history_big_init(&hist);

    uint64_t now = monotonic_millis();
    ttak_bigint_t current;
    ttak_bigint_init_copy(&current, start_val, now);
    history_big_insert(&hist, &current, start_step);

    ttak_bigint_t max_seen;
    ttak_bigint_init_copy(&max_seen, start_val, now);
    out->max_bits = ttak_bigint_get_bit_length(&max_seen);
    uint32_t max_step_index = start_step;

    uint32_t steps = start_step;
    while (true) {
        if (ttak_bigint_cmp(&current, &max_seen) > 0) {
            ttak_bigint_copy(&max_seen, &current, now);
            out->max_bits = ttak_bigint_get_bit_length(&max_seen);
            max_step_index = steps;
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

        now = monotonic_millis();
        ttak_bigint_t next;
        ttak_bigint_init(&next, now);

        if (!sum_proper_divisors_big(&current, &next, now)) {
            uint64_t approx = 0;
            ttak_bigint_export_u64(&current, &approx);
            log_big_sum_failure(out->seed, steps, approx, &current, "big-sequence");
            ttak_bigint_free(&next, now);
            break;
        }
        ttak_atomic_inc64(&g_total_probes);
        steps++;

        if (ttak_bigint_is_zero(&next) || ttak_bigint_cmp_u64(&next, 1) == 0) {
            out->terminated = true;
            if (!ttak_bigint_export_u64(&next, &out->final_value)) {
                out->final_value = UINT64_MAX;
            }
            ttak_bigint_free(&next, now);
            break;
        }

        uint32_t prev_step = 0;
        if (history_big_contains(&hist, &next, &prev_step)) {
            out->entered_cycle = true;
            out->cycle_length = steps - prev_step;
            if (!ttak_bigint_export_u64(&next, &out->final_value)) {
                out->final_value = UINT64_MAX;
            }
            ttak_bigint_free(&next, now);
            break;
        }

        history_big_insert(&hist, &next, steps);
        ttak_bigint_copy(&current, &next, now);
        ttak_bigint_free(&next, now);
    }

    out->steps = steps;
    out->max_step_index = max_step_index;
    out->max_bits = ttak_bigint_get_bit_length(&max_seen);
    ttak_bigint_to_hex_hash(&max_seen, out->max_hash);
    ttak_bigint_format_prefix(&max_seen, out->max_prefix, sizeof(out->max_prefix));
    if (!aliquot_outcome_set_decimal_from_bigint(out, &max_seen, monotonic_millis())) {
        out->max_dec_digits = 0;
    }
    if (out->overflow) {
        out->max_value = UINT64_MAX;
    }

    ttak_bigint_free(&max_seen, now);
    ttak_bigint_free(&current, now);
    history_big_destroy(&hist);
}

static void run_aliquot_sequence(uint64_t seed, uint32_t max_steps, uint64_t time_budget_ms, bool allow_bigints, aliquot_outcome_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->seed = seed;
    out->max_value = seed;
    out->final_value = seed;
    out->max_bits = bit_length_u64(seed);
    out->max_step_index = 0;
    uint64_t start_ms = monotonic_millis();
    uint64_t start_us = monotonic_micros();

    history_table_t hist;
    history_init(&hist);
    history_insert(&hist, seed, 0);

    uint64_t current = seed;
    uint32_t steps = 0;
    while (true) {
        if (steps == 0 && is_catalog_value(current)) {
            out->catalog_hit = true;
            out->final_value = current;
            break;
        }
        if (max_steps > 0 && steps >= max_steps) {
            out->hit_limit = true;
            break;
        }
        if (time_budget_ms > 0) {
            uint64_t now = monotonic_millis();
            if (now - start_ms >= time_budget_ms) {
                out->hit_limit = true;
                out->time_budget_hit = true;
                break;
            }
        }
        if (ttak_atomic_read64(&shutdown_requested)) break;

        uint64_t next = 0;
        bool ok = sum_proper_divisors_u64_checked(current, &next);
        ttak_atomic_inc64(&g_total_probes);
        steps++;

        if (!ok) {
            out->overflow = true;
            if (!allow_bigints) break;

            uint64_t now = monotonic_millis();
            ttak_bigint_t big_current;
            ttak_bigint_init(&big_current, now);
            ttak_bigint_set_u64(&big_current, current, now);
            ttak_bigint_t big_next;
            ttak_bigint_init(&big_next, now);
            if (!sum_proper_divisors_big(&big_current, &big_next, now)) {
                log_big_sum_failure(seed, steps, current, &big_current, "bridge");
                ttak_bigint_free(&big_next, now);
                ttak_bigint_free(&big_current, now);
                break;
            }
            run_aliquot_sequence_big(&big_next, steps, max_steps, time_budget_ms, out, start_ms);
            ttak_bigint_free(&big_next, now);
            ttak_bigint_free(&big_current, now);
            break;
        }

        if (next > out->max_value) {
            out->max_value = next;
            uint32_t bits = bit_length_u64(next);
            if (bits > out->max_bits) out->max_bits = bits;
            out->max_step_index = steps;
        }

        if (next <= 1) {
            out->terminated = true;
            out->final_value = next;
            break;
        }
        uint32_t prev_step = 0;
        if (history_contains(&hist, next, &prev_step)) {
            out->entered_cycle = true;
            out->cycle_length = steps - prev_step;
            out->final_value = next;
            if (out->cycle_length <= 2) {
                if (out->cycle_length == 1 && next == seed) out->perfect = true;
                else out->amicable = true;
            }
            break;
        }
        if (is_catalog_value(next)) {
            out->catalog_hit = true;
            out->final_value = next;
            break;
        }
        history_insert(&hist, next, steps);
        current = next;
    }
    if (!out->terminated && !out->entered_cycle && !out->overflow) {
        out->final_value = current;
    }
    out->steps = steps;
    uint64_t elapsed_us = monotonic_micros() - start_us;
    out->wall_time_us = elapsed_us;
    out->wall_time_ms = elapsed_us / 1000ULL;
    if (out->wall_time_ms == 0 && elapsed_us > 0) {
        out->wall_time_ms = 1;
    }
    if (!out->max_value_dec) aliquot_outcome_set_decimal_from_u64(out, out->max_value);
    history_destroy(&hist);
}

static double compute_probe_score(const aliquot_outcome_t *out) {
    double span = (out->seed > 0) ? (double)out->max_value / (double)out->seed : 1.0;
    if (span < 1.0) span = 1.0;
    double log_height = log(span);
    double base = (double)out->steps * 0.75 + log_height * 8.0;
    if (out->hit_limit) base += 30.0;
    if (out->max_value > 1000000000ULL) base += 25.0;
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
    long double ratio = (long double)out->max_value / (long double)UINT64_MAX;
    if (ratio < 0.0L) ratio = 0.0L;
    if (ratio > 1.0L) ratio = 1.0L;
    return (double)(ratio * 60.0L);
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
        state->found_records[state->found_count++] = *params->record;
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
        state->jump_records[state->jump_count++] = *params->record;
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
        state->track_records[state->track_count++] = *params->record;
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
    for (size_t i = state->persisted_found_count; i < state->found_count; ++i) {
        const found_record_t *rec = &state->found_records[i];
        fprintf(fp, "{\"seed\":%" PRIu64 ",\"steps\":%" PRIu64 ",\"max\":%" PRIu64 ",\"final\":%" PRIu64 ",\"cycle\":%u,\"status\":\"%s\",\"source\":\"%s\"}\n",
                rec->seed, rec->steps, rec->max_value, rec->final_value,
                rec->cycle_length, rec->status, rec->provenance);
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
    for (size_t i = state->persisted_jump_count; i < state->jump_count; ++i) {
        const jump_record_t *rec = &state->jump_records[i];
        fprintf(fp, "{\"seed\":%" PRIu64 ",\"steps\":%" PRIu64 ",\"max\":%" PRIu64 ",\"score\":%.2f,\"overflow\":%.3f}\n",
                rec->seed, rec->preview_steps, rec->preview_max, rec->score, rec->overflow_pressure);
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
    for (size_t i = state->persisted_track_count; i < state->track_count; ++i) {
        track_record_t *rec = &state->track_records[i];
        fprintf(fp, "{\"seed\":%" PRIu64 ",\"steps\":%" PRIu64 ",\"bits\":%u,\"digits\":%u,"
                "\"hash\":\"%s\",\"prefix\":\"%s\",\"ended\":\"%s\",\"ended_by\":\"%s\",\"wall_ms\":%" PRIu64 ",\"wall_us\":%" PRIu64 ","
                "\"budget_ms\":%" PRIu64 ",\"score\":%.2f,\"priority\":%u,\"max_step\":%u,\"max_value\":\"%s\"}\n",
                rec->seed, rec->steps, rec->max_bits, rec->max_dec_digits,
                rec->max_hash, rec->max_prefix, rec->ended, rec->ended_by, rec->wall_time_ms, rec->wall_time_us,
                rec->budget_ms, rec->scout_score, rec->priority, rec->max_step,
                rec->max_value_dec ? rec->max_value_dec : "unknown");
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
    ttak_mutex_destroy(&g_ledger_state.lock);
}

static void append_found_record(const aliquot_outcome_t *out, const char *source) {
    if (!out) return;
    found_record_t rec = {0};
    rec.seed = out->seed;
    rec.steps = out->steps;
    rec.max_value = out->max_value;
    rec.final_value = out->final_value;
    rec.cycle_length = out->cycle_length;
    snprintf(rec.status, sizeof(rec.status), "%s", classify_outcome(out));
    if (source) snprintf(rec.provenance, sizeof(rec.provenance), "%s", source);
    else rec.provenance[0] = '\0';
    if (!ledger_store_found_record(&rec)) return;
    printf("[ALIQUOT] seed=%" PRIu64 " steps=%" PRIu64 " status=%s via %s\n",
            rec.seed, rec.steps, rec.status,
            rec.provenance[0] ? rec.provenance : "unknown");
    ttak_atomic_inc64(&g_total_sequences);
}

static void append_jump_record(uint64_t seed, uint64_t steps, uint64_t max_value, double score, double overflow_pressure) {
    jump_record_t rec = {
        .seed = seed,
        .preview_steps = steps,
        .preview_max = max_value,
        .score = score,
        .overflow_pressure = overflow_pressure
    };
    if (!ledger_store_jump_record(&rec)) return;
    printf("[SCOUT] seed=%" PRIu64 " steps=%" PRIu64 " max=%" PRIu64 " score=%.2f overflow=%.2f\n",
            seed, steps, max_value, score, overflow_pressure);
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
    if (out->terminated) { snprintf(dest, dest_cap, "reached_%" PRIu64, out->final_value); return; }
    if (out->hit_limit) { snprintf(dest, dest_cap, "step_limit"); return; }
    snprintf(dest, dest_cap, "open");
}

static bool aliquot_outcome_set_decimal_from_u64(aliquot_outcome_t *out, uint64_t value) {
    if (!out) return false;
    char tmp[32];
    int written = snprintf(tmp, sizeof(tmp), "%" PRIu64, value);
    if (written < 0) return false;
    uint64_t now = monotonic_millis();
    char *dst = ttak_mem_alloc((size_t)written + 1, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!dst) return false;
    memcpy(dst, tmp, (size_t)written + 1);
    if (out->max_value_dec) ttak_mem_free(out->max_value_dec);
    out->max_value_dec = dst;
    out->max_dec_digits = (uint32_t)written;
    return true;
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
    if (out->max_value_dec) { ttak_mem_free(out->max_value_dec); out->max_value_dec = NULL; }
}

static void capture_track_metrics(aliquot_outcome_t *out, const aliquot_job_t *job, uint64_t budget_ms, track_record_t *rec) {
    if (!out || !rec) return;
    memset(rec, 0, sizeof(*rec));
    rec->seed = out->seed;
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

    uint64_t now = monotonic_millis();
    ttak_bigint_t max_bi;
    ttak_bigint_init(&max_bi, now);
    if (out->overflow) {
        rec->max_bits = out->max_bits;
        snprintf(rec->max_hash, sizeof(rec->max_hash), "%s", out->max_hash);
        snprintf(rec->max_prefix, sizeof(rec->max_prefix), "%s", out->max_prefix);
    } else {
        if (ttak_bigint_set_u64(&max_bi, out->max_value, now)) {
            rec->max_bits = ttak_bigint_get_bit_length(&max_bi);
            ttak_bigint_to_hex_hash(&max_bi, rec->max_hash);
            ttak_bigint_format_prefix(&max_bi, rec->max_prefix, sizeof(rec->max_prefix));
        }
    }
    ttak_bigint_free(&max_bi, now);
}

static void append_track_record(aliquot_outcome_t *out, const aliquot_job_t *job, uint64_t budget_ms) {
    if (!out) return;
    track_record_t rec = {0};
    capture_track_metrics(out, job, budget_ms, &rec);
    if (!ledger_store_track_record(&rec)) {
        if (rec.max_value_dec) ttak_mem_free(rec.max_value_dec);
        return;
    }
    printf("[TRACK] seed=%" PRIu64 " bits=%u ended_by=%s\n",
           rec.seed, rec.max_bits, rec.ended_by);
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
    uint64_t pending[JOB_QUEUE_CAP];
    size_t count = pending_queue_snapshot(pending, JOB_QUEUE_CAP);
    FILE *fp = fopen(g_queue_state_path, "w");
    if (!fp) return;
    fprintf(fp, "{\"pending\":[");
    for (size_t i = 0; i < count; ++i) fprintf(fp, "%s%" PRIu64, (i == 0) ? "" : ",", pending[i]);
    fprintf(fp, "],\"ts\":%" PRIu64 "}\n", monotonic_millis());
    fclose(fp);
}

static void flush_ledgers(void) {
    ttak_mutex_lock(&g_disk_lock);
    persist_found_records(); persist_jump_records(); persist_track_records(); persist_queue_state();
    ttak_atomic_write64(&g_last_persist_ms, monotonic_millis());
    ttak_mutex_unlock(&g_disk_lock);
}

static void maybe_flush_ledgers(void) {
    uint64_t now = monotonic_millis();
    if (now - ttak_atomic_read64(&g_last_persist_ms) >= FLUSH_INTERVAL_MS) flush_ledgers();
}

static void load_found_records(void) {
    FILE *fp = fopen(g_found_log_path, "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        found_record_t rec = {0};
        if (sscanf(line, "{\"seed\":%" SCNu64 ",\"steps\":%" SCNu64 ",\"max\":%" SCNu64 ",\"final\":%" SCNu64 ",\"cycle\":%u,\"status\":\"%23[^\"]\",\"source\":\"%15[^\"]\"}",
                   &rec.seed, &rec.steps, &rec.max_value, &rec.final_value, &rec.cycle_length, rec.status, rec.provenance) >= 5) {
            rehydrate_found_record(&rec); seed_registry_mark(rec.seed);
        }
    }
    fclose(fp);
    ledger_mark_found_persisted();
}

static void load_jump_records(void) {
    FILE *fp = fopen(g_jump_log_path, "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        jump_record_t rec = {0};
        if (sscanf(line, "{\"seed\":%" SCNu64 ",\"steps\":%" SCNu64 ",\"max\":%" SCNu64 ",\"score\":%lf,\"overflow\":%lf",
                   &rec.seed, &rec.preview_steps, &rec.preview_max, &rec.score, &rec.overflow_pressure) >= 4) {
            rehydrate_jump_record(&rec);
        }
    }
    fclose(fp);
    ledger_mark_jump_persisted();
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

static void load_track_records(void) {
    FILE *fp = fopen(g_track_log_path, "r");
    if (!fp) return;
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        track_record_t rec = {0}; bool ok = true;
        ok &= json_extract_uint64(line, "seed", &rec.seed);
        ok &= json_extract_uint64(line, "steps", &rec.steps);
        ok &= json_extract_uint32(line, "bits", &rec.max_bits);
        ok &= json_extract_uint32(line, "digits", &rec.max_dec_digits);
        ok &= json_extract_string(line, "hash", rec.max_hash, sizeof(rec.max_hash));
        ok &= json_extract_string(line, "prefix", rec.max_prefix, sizeof(rec.max_prefix));
        ok &= json_extract_string(line, "ended", rec.ended, sizeof(rec.ended));
        if (!json_extract_string(line, "ended_by", rec.ended_by, sizeof(rec.ended_by)))
            snprintf(rec.ended_by, sizeof(rec.ended_by), "%s", rec.ended);
        ok &= json_extract_uint64(line, "wall_ms", &rec.wall_time_ms);
        if (!json_extract_uint64(line, "wall_us", &rec.wall_time_us)) {
            rec.wall_time_us = rec.wall_time_ms * 1000ULL;
        }
        ok &= json_extract_uint64(line, "budget_ms", &rec.budget_ms);
        ok &= json_extract_double(line, "score", &rec.scout_score);
        ok &= json_extract_uint32(line, "priority", &rec.priority);
        json_extract_uint32(line, "max_step", &rec.max_step);
        if (ok) rehydrate_track_record(&rec);
    }
    fclose(fp);
    ledger_mark_track_persisted();
}

static bool enqueue_job(aliquot_job_t *job, const char *source_tag) {
    if (!job || !g_thread_pool) return false;
    if (!pending_queue_add(job->seed)) return false;
    uint64_t now = monotonic_millis();
    ttak_future_t *future = ttak_thread_pool_submit_task(g_thread_pool, worker_process_job_wrapper, job, job->priority, now);
    if (!future) { pending_queue_remove(job->seed); return false; }
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
    char *s = strchr(buf, '['), *e = s ? strchr(s, ']') : NULL;
    if (s && e && e > s) {
        char *p = s + 1;
        while (p < e) {
            while (p < e && !isdigit((unsigned char)*p)) p++;
            if (p >= e) break;
            uint64_t seed = strtoull(p, &p, 10);
            if (seed > 1 && seed_registry_try_add(seed)) {
                aliquot_job_t *job = ttak_mem_alloc(sizeof(aliquot_job_t), __TTAK_UNSAFE_MEM_FOREVER__, monotonic_millis());
                if (job) {
                    memset(job, 0, sizeof(*job)); job->seed = seed; job->priority = 1;
                    snprintf(job->provenance, sizeof(job->provenance), "checkpoint");
                    if (!enqueue_job(job, "checkpoint")) ttak_mem_free(job);
                }
            }
        }
    }
    ttak_mem_free(buf);
}

static void *worker_process_job_wrapper(void *arg) {
    aliquot_job_t *job = (aliquot_job_t *)arg;
    pending_queue_remove(job->seed);
    process_job(job);
    ttak_mem_free(job);
    return NULL;
}

static void process_job(const aliquot_job_t *job) {
    if (!job) return;
    aliquot_outcome_t outcome;
    uint64_t budget_ms = determine_time_budget(job);
    uint32_t max_steps = (job->priority >= 3) ? 0 : LONG_RUN_MAX_STEPS;
    run_aliquot_sequence(job->seed, max_steps, budget_ms, true, &outcome);

    if (outcome.max_bits > 64 && outcome.hit_limit) {
        uint64_t now = monotonic_millis();
        aliquot_job_t *retry = ttak_mem_alloc(sizeof(aliquot_job_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
        if (retry) {
            memset(retry, 0, sizeof(*retry)); retry->seed = job->seed; retry->priority = 10;
            retry->scout_score = job->scout_score; snprintf(retry->provenance, sizeof(retry->provenance), "retry-big");
            if (enqueue_job(retry, "retry-limit")) {
                append_track_record(&outcome, job, budget_ms); maybe_flush_ledgers(); aliquot_outcome_cleanup(&outcome);
                return;
            } else ttak_mem_free(retry);
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
        if (pending_queue_depth() > JOB_QUEUE_CAP - 8) { responsive_sleep(SCOUT_SLEEP_MS); continue; }
        uint64_t seed = random_seed_between(SCOUT_MIN_SEED, SCOUT_MAX_SEED);
        if (!seed_registry_try_add(seed)) { responsive_sleep(10); continue; }
        scan_result_t sr;
        if (!frontier_accept_seed(seed, &sr)) { responsive_sleep(5); continue; }
        aliquot_outcome_t probe; run_aliquot_sequence(seed, SCOUT_PREVIEW_STEPS, 0, false, &probe);
        double score; double op = compute_overflow_pressure(&probe);
        if (looks_long(&probe, &score)) {
            append_jump_record(seed, probe.steps, probe.max_value, score, op);
            aliquot_job_t *job = ttak_mem_alloc(sizeof(aliquot_job_t), __TTAK_UNSAFE_MEM_FOREVER__, monotonic_millis());
            if (job) {
                memset(job, 0, sizeof(*job)); job->seed = seed;
                job->priority = (probe.overflow || op >= 45.0) ? 3 : 2;
                job->preview_steps = probe.steps; job->preview_max = probe.max_value;
                job->preview_overflow = probe.overflow || (op >= 45.0); job->scout_score = score;
                snprintf(job->provenance, sizeof(job->provenance), "scout");
                if (!enqueue_job(job, "scout")) ttak_mem_free(job);
                else maybe_flush_ledgers();
            }
        }
        aliquot_outcome_cleanup(&probe); responsive_sleep(SCOUT_SLEEP_MS);
    }
    return NULL;
}

int main(void) {
    printf("[ALIQUOT] Booting aliquot tracker...\n");
    seed_rng(); configure_state_paths(); ensure_state_dir(); init_catalog_filters();
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
    pthread_t scout_thread; pthread_create(&scout_thread, NULL, scout_main, NULL);

    /* SELF-SEEDING BOOTSTRAP: Ensure we have work immediately */
    if (pending_queue_depth() == 0) {
        printf("[ALIQUOT] Warm-up: Seeding initial jobs manually...\n");
        for (int i = 0; i < (int)cpus; i++) {
            uint64_t seed = random_seed_between(SCOUT_MIN_SEED, SCOUT_MAX_SEED);
            if (seed_registry_try_add(seed)) {
                aliquot_job_t *job = ttak_mem_alloc(sizeof(aliquot_job_t), __TTAK_UNSAFE_MEM_FOREVER__, monotonic_millis());
                if (job) {
                    memset(job, 0, sizeof(*job)); job->seed = seed; job->priority = 1;
                    snprintf(job->provenance, sizeof(job->provenance), "warmup");
                    if (!enqueue_job(job, "warmup")) ttak_mem_free(job);
                }
            }
        }
    }

    while (!ttak_atomic_read64(&shutdown_requested)) {
        size_t qd = pending_queue_depth();

        /* ACTIVE SCHEDULING: Hunt for seeds if queue is low */
        if (qd < (size_t)cpus) {
            uint64_t ns = random_seed_between(SCOUT_MIN_SEED, SCOUT_MAX_SEED);
            if (seed_registry_try_add(ns)) {
                aliquot_job_t *job = ttak_mem_alloc(sizeof(aliquot_job_t), __TTAK_UNSAFE_MEM_FOREVER__, monotonic_millis());
                if (job) {
                    memset(job, 0, sizeof(*job)); job->seed = ns; job->priority = 1;
                    snprintf(job->provenance, sizeof(job->provenance), "main_hunt");
                    if (!enqueue_job(job, "main_hunt")) ttak_mem_free(job);
                }
            }
        }

        responsive_sleep(SCOUT_SLEEP_MS);
        maybe_flush_ledgers();

        uint64_t completed = ttak_atomic_read64(&g_total_sequences);
        printf("[ALIQUOT] queue=%zu completed=%" PRIu64 " probes=%" PRIu64 "\n",
               qd, completed, ttak_atomic_read64(&g_total_probes));
    }

    printf("[ALIQUOT] Shutdown requested. Waiting for threads to exit...\n");
    pthread_join(scout_thread, NULL);
    ttak_thread_pool_destroy(g_thread_pool);
    flush_ledgers();
    ledger_destroy_owner();
    printf("[ALIQUOT] Shutdown complete.\n");
    return 0;
}
