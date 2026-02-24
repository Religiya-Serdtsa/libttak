/**
 * @file main.c
 * @brief Deterministic aliquot period-3 sociable scanner with strict journal resume and duplicate-free dispatch.
 *
 * Key design:
 * - Recovery/Frontier/Dispatch seeds are BigInt-first to avoid early truncation and to support growth.
 * - A u64 cache may be derived when the current BigInt fits in uint64 for optional fast-path decisions.
 * - Journal parsing is strict and canonical. Any parsing failure is fatal.
 * - Duplicate dispatch is prevented by an in-memory inflight map plus persistent journal state.
 * - Shutdown is deterministic: graceful stop, then watchdog-enforced hard exit if blocked.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <ttak/atomic/atomic.h>
#include <ttak/math/bigint.h>
#include <ttak/math/sum_divisors.h>
#include <ttak/mem/mem.h>
#include <ttak/security/sha256.h>
#include <ttak/thread/pool.h>
#include <ttak/timing/timing.h>
#include <ttak/script/bigscript.h>

#define BLOCK_SIZE              10000ULL
#define DEFAULT_START_SEED      0ULL

#define STATE_DIR               "/opt/aliquot-3"
#define HASH_LOG_NAME           "range_proofs.log"
#define FOUND_LOG_NAME          "sociable_found.jsonl"
#define CHECKPOINT_FILE         "scanner_checkpoint.txt"
#define JOURNAL_FILE            "task_journal.jnl"
#define LOCK_FILE               "scanner.lock"

#define STATUS_INTERVAL_MS      5000ULL
#define CHECKPOINT_INTERVAL_MS  5000ULL
#define DISPATCH_POLL_NS        2000000L
#define MAX_INFLIGHT_FACTOR     2
#define SHUTDOWN_TIMEOUT_S      30
#define PROGRESS_FLUSH_STRIDE   1024ULL

/* -------------------------------------------------------------------------- */
/* Time helper                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Returns a monotonic timestamp in milliseconds.
 * @return Monotonic time in milliseconds.
 */
static uint64_t monotonic_millis(void) {
    return ttak_get_tick_count();
}

/* -------------------------------------------------------------------------- */
/* Canonical decimal validation                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Checks whether a string is a canonical non-negative decimal integer.
 *
 * Canonical format:
 * - "0" is allowed
 * - non-zero values must not have leading zeros
 * - digits only
 *
 * @param s Null-terminated string to validate.
 * @return true if canonical decimal, false otherwise.
 */
static bool is_canonical_decimal(const char *s) {
    if (!s || !*s) return false;
    if (s[0] == '0' && s[1] != '\0') return false;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return false;
    }
    return true;
}

/* -------------------------------------------------------------------------- */
/* BigInt-first core seed type                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief BigInt-first numeric state used for recovery/frontiers/dispatch.
 *
 * This always maintains a BigInt as the authoritative value.
 * A derived uint64 cache is optional and only valid when fits_u64 is true.
 */
typedef struct core_seed_t {
    ttak_bigint_t big;     /**< Authoritative value. */
    bool fits_u64;         /**< Whether the value fits into uint64_t. */
    uint64_t u64_cache;    /**< Cached uint64_t value when fits_u64 is true. */
} core_seed_t;

static char *core_seed_to_decimal(const core_seed_t *n, uint64_t now);
static bool core_seed_parse_decimal(core_seed_t *out, const char *s, uint64_t now);

/**
 * @brief Initializes core_seed_t to zero (BigInt-first).
 * @param n Target core_seed_t.
 * @param now Monotonic timestamp for allocator/arena integration.
 */
static void core_seed_init_zero(core_seed_t *n, uint64_t now) {
    ttak_bigint_init_u64(&n->big, 0, now);
    n->fits_u64 = true;
    n->u64_cache = 0;
}

/**
 * @brief Frees BigInt resources held by core_seed_t.
 * @param n Target core_seed_t.
 * @param now Monotonic timestamp for allocator/arena integration.
 */
static void core_seed_free(core_seed_t *n, uint64_t now) {
    ttak_bigint_free(&n->big, now);
    n->fits_u64 = false;
    n->u64_cache = 0;
}

/**
 * @brief Refreshes the uint64 cache if the BigInt fits in uint64.
 *
 * This function never changes the authoritative value.
 *
 * @param n Target core_seed_t.
 * @param now Monotonic timestamp.
 * @return true on success, false on conversion failure.
 */
static bool core_seed_refresh_u64_cache(core_seed_t *n, uint64_t now) {
    (void)now;
    uint64_t v = 0;
    if (ttak_bigint_export_u64(&n->big, &v)) {
        n->fits_u64 = true;
        n->u64_cache = v;
        return true;
    }
    n->fits_u64 = false;
    n->u64_cache = 0;
    return true;
}

/**
 * @brief Copies core_seed_t value.
 * @param dst Destination.
 * @param src Source.
 * @param now Monotonic timestamp.
 * @return true on success, false on failure.
 */
static bool core_seed_copy(core_seed_t *dst, const core_seed_t *src, uint64_t now) {
    char *s = core_seed_to_decimal(src, now);
    if (!s) return false;
    bool ok = core_seed_parse_decimal(dst, s, now);
    ttak_mem_free(s);
    return ok;
}

/**
 * @brief Parses a canonical decimal into core_seed_t (BigInt-first).
 * @param out Destination core_seed_t.
 * @param s Canonical decimal string.
 * @param now Monotonic timestamp.
 * @return true on success, false on failure.
 */
static bool core_seed_parse_decimal(core_seed_t *out, const char *s, uint64_t now) {
    if (!is_canonical_decimal(s)) return false;

    ttak_bigint_free(&out->big, now);
    ttak_bigint_init_u64(&out->big, 0, now);

    ttak_bigint_t tmp;
    ttak_bigint_init(&tmp, now);

    for (const char *p = s; *p; p++) {
        uint64_t d = (uint64_t)(*p - '0');
        if (!ttak_bigint_mul_u64(&tmp, &out->big, 10, now)) {
            ttak_bigint_free(&tmp, now);
            return false;
        }
        if (!ttak_bigint_add_u64(&out->big, &tmp, d, now)) {
            ttak_bigint_free(&tmp, now);
            return false;
        }
    }

    ttak_bigint_free(&tmp, now);
    return core_seed_refresh_u64_cache(out, now);
}

/**
 * @brief Converts core_seed_t to a canonical decimal string.
 *
 * The returned buffer is allocated and must be freed with ttak_mem_free.
 *
 * @param n Source core_seed_t.
 * @param now Monotonic timestamp.
 * @return Allocated decimal string, or NULL on failure.
 */
static char *core_seed_to_decimal(const core_seed_t *n, uint64_t now) {
    char *s = ttak_bigint_to_string(&n->big, now);
    if (!s) return NULL;

    if (s[0] == '0' && s[1] == '\0') return s;

    size_t i = 0;
    while (s[i] == '0') i++;
    if (i == 0) return s;

    size_t len = strlen(s);
    if (i >= len) {
        ttak_mem_free(s);
        char *z = (char *)ttak_mem_alloc(2, __TTAK_UNSAFE_MEM_FOREVER__, now);
        if (!z) return NULL;
        z[0] = '0';
        z[1] = '\0';
        return z;
    }

    size_t new_len = len - i;
    char *out = (char *)ttak_mem_alloc(new_len + 1, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!out) {
        ttak_mem_free(s);
        return NULL;
    }
    memcpy(out, s + i, new_len);
    out[new_len] = '\0';
    ttak_mem_free(s);
    return out;
}

/**
 * @brief Compares two core_seed_t values safely.
 *
 * Fallback to string comparison if BigInt comparison is suspected to be buggy.
 *
 * @param a Left operand.
 * @param b Right operand.
 * @return -1 if a<b, 0 if a==b, 1 if a>b.
 */
static int safe_core_seed_cmp(const core_seed_t *a, const core_seed_t *b) {
    if (a->fits_u64 && b->fits_u64) {
        if (a->u64_cache < b->u64_cache) return -1;
        if (a->u64_cache > b->u64_cache) return 1;
        return 0;
    }

    char *sa = ttak_bigint_to_string(&a->big, monotonic_millis());
    char *sb = ttak_bigint_to_string(&b->big, monotonic_millis());
    
    int res = 0;
    if (sa && sb) {
        size_t la = strlen(sa);
        size_t lb = strlen(sb);
        if (la < lb) res = -1;
        else if (la > lb) res = 1;
        else res = strcmp(sa, sb);
    } else {
        /* Fallback if stringify fails */
        res = ttak_bigint_cmp(&a->big, &b->big);
    }

    if (sa) ttak_mem_free(sa);
    if (sb) ttak_mem_free(sb);
    return (res < 0) ? -1 : (res > 0) ? 1 : 0;
}

/**
 * @brief Adds an unsigned delta to core_seed_t (BigInt-first).
 * @param out Destination.
 * @param a Input value.
 * @param delta Unsigned delta.
 * @param now Monotonic timestamp.
 * @return true on success, false on failure.
 */
static bool core_seed_add_u64(core_seed_t *out, const core_seed_t *a, uint64_t delta, uint64_t now) {
    ttak_bigint_free(&out->big, now);
    ttak_bigint_init(&out->big, now);
    if (!ttak_bigint_copy(&out->big, &a->big, now)) return false;
    if (!ttak_bigint_add_u64(&out->big, &out->big, delta, now)) return false;
    out->fits_u64 = false;
    out->u64_cache = 0;
    return core_seed_refresh_u64_cache(out, now);
}

/**
 * @brief Adds BLOCK_SIZE to core_seed_t in-place.
 * @param n Target core_seed_t.
 * @param now Monotonic timestamp.
 * @return true on success, false on failure.
 */
static bool core_seed_add_block_inplace(core_seed_t *n, uint64_t now) {
    if (!ttak_bigint_add_u64(&n->big, &n->big, BLOCK_SIZE, now)) return false;
    return core_seed_refresh_u64_cache(n, now);
}

/* -------------------------------------------------------------------------- */
/* Work task                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief One scan task. start is authoritative BigInt-first.
 */
typedef struct scan_task_t {
    uint64_t task_id;
    uint64_t count;
    core_seed_t start;
} scan_task_t;

/* -------------------------------------------------------------------------- */
/* Global runtime state                                                         */
/* -------------------------------------------------------------------------- */

static volatile uint64_t g_shutdown_requested = 0;
static volatile uint64_t g_hard_shutdown_requested = 0;

static volatile uint64_t g_total_scanned = 0;
static volatile uint64_t g_inflight = 0;

static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_journal_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;

static ttak_bigscript_program_t *g_script_prog = NULL;
static char g_script_hash_hex[65] = {0};
static char g_resume_script_hash_hex[65] = {0};


static char g_hash_log_path[4096];
static char g_found_log_path[4096];
static char g_checkpoint_path[4096];
static char g_journal_path[4096];
static char g_lock_path[4096];

static int g_lock_fd = -1;

static core_seed_t g_next_dispatch;
static core_seed_t g_verified_frontier;
static uint64_t g_next_task_id = 1;

/* -------------------------------------------------------------------------- */
/* State directory / lock                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Ensures that a directory exists, creating it if required.
 * @param path Directory path.
 */
static void ensure_dir_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "[FATAL] State path exists but is not a directory: %s\n", path);
            exit(EXIT_FAILURE);
        }
        return;
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "[FATAL] Unable to create state directory: %s (%s)\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Configures state file paths based on ALIQUOT_STATE_DIR or default.
 */
static void configure_state_paths(void) {
    const char *state_dir = getenv("ALIQUOT_STATE_DIR");
    if (!state_dir || !*state_dir) state_dir = STATE_DIR;

    ensure_dir_exists(state_dir);

    snprintf(g_hash_log_path, sizeof(g_hash_log_path), "%s/%s", state_dir, HASH_LOG_NAME);
    snprintf(g_found_log_path, sizeof(g_found_log_path), "%s/%s", state_dir, FOUND_LOG_NAME);
    snprintf(g_checkpoint_path, sizeof(g_checkpoint_path), "%s/%s", state_dir, CHECKPOINT_FILE);
    snprintf(g_journal_path, sizeof(g_journal_path), "%s/%s", state_dir, JOURNAL_FILE);
    snprintf(g_lock_path, sizeof(g_lock_path), "%s/%s", state_dir, LOCK_FILE);
}

/**
 * @brief Acquires an exclusive instance lock to prevent multiple concurrent scanners.
 */
static void acquire_single_instance_lock(void) {
    g_lock_fd = open(g_lock_path, O_CREAT | O_RDWR, 0644);
    if (g_lock_fd < 0) {
        fprintf(stderr, "[FATAL] Unable to open lock file %s: %s\n", g_lock_path, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (flock(g_lock_fd, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr, "[FATAL] Another scanner instance is already running: %s\n", g_lock_path);
        close(g_lock_fd);
        g_lock_fd = -1;
        exit(EXIT_FAILURE);
    }
}

/* -------------------------------------------------------------------------- */
/* Durable append                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Writes the full buffer to a file descriptor.
 * @param fd File descriptor.
 * @param buf Buffer pointer.
 * @param len Buffer length.
 * @return true on success, false on failure.
 */
static bool write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        off += (size_t)n;
    }
    return true;
}

/**
 * @brief Appends a line to a file and fsync()s it.
 * @param path File path.
 * @param line Line content (must include newline).
 * @return true on success, false on failure.
 */
static bool append_line_fsync(const char *path, const char *line) {
    int fd = open(path, O_CREAT | O_APPEND | O_WRONLY, 0644);
    if (fd < 0) return false;

    bool ok = write_all(fd, line, strlen(line));
    if (ok) ok = (fsync(fd) == 0);
    close(fd);
    return ok;
}

/**
 * @brief Appends a line to a file without fsync() (best effort).
 * @param path File path.
 * @param line Line content (must include newline).
 * @return true on success, false on failure.
 */
static bool append_line_best_effort(const char *path, const char *line) {
    int fd = open(path, O_CREAT | O_APPEND | O_WRONLY, 0644);
    if (fd < 0) return false;
    bool ok = write_all(fd, line, strlen(line));
    close(fd);
    return ok;
}

/* -------------------------------------------------------------------------- */
/* String-key hash map (range_start -> state)                                   */
/* -------------------------------------------------------------------------- */

typedef struct str_entry_t {
    char *key;
    uint8_t state; /* 0=empty, 1=reserved, 2=done */
} str_entry_t;

typedef struct str_map_t {
    str_entry_t *tab;
    size_t cap;
    size_t len;
} str_map_t;

/**
 * @brief 64-bit hash for strings.
 * @param s Null-terminated string.
 * @return Hash value.
 */
static uint64_t hash_str64(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 1099518103934665603ULL;
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

static bool str_map_init(str_map_t *m, size_t cap_pow2) {
    if (!m) return false;
    size_t cap = 1;
    if (cap_pow2 < 1024) cap_pow2 = 1024;
    while (cap < cap_pow2) cap <<= 1;

    m->tab = (str_entry_t *)ttak_mem_alloc(cap * sizeof(str_entry_t),
                                           __TTAK_UNSAFE_MEM_FOREVER__,
                                           monotonic_millis());
    if (!m->tab) return false;
    memset(m->tab, 0, cap * sizeof(str_entry_t));
    m->cap = cap;
    m->len = 0;
    return true;
}

static void str_map_free(str_map_t *m) {
    if (!m || !m->tab) return;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->tab[i].key) ttak_mem_free(m->tab[i].key);
    }
    ttak_mem_free(m->tab);
    m->tab = NULL;
    m->cap = 0;
    m->len = 0;
}

static bool str_map_rehash(str_map_t *m, size_t new_cap_pow2) {
    str_map_t nm;
    memset(&nm, 0, sizeof(nm));
    if (!str_map_init(&nm, new_cap_pow2)) return false;

    for (size_t i = 0; i < m->cap; i++) {
        if (!m->tab[i].key) continue;
        char *k = m->tab[i].key;
        uint8_t st = m->tab[i].state;

        size_t mask = nm.cap - 1;
        size_t j = (size_t)hash_str64(k) & mask;
        while (nm.tab[j].key) j = (j + 1) & mask;

        nm.tab[j].key = k;
        nm.tab[j].state = st;
        nm.len++;
        m->tab[i].key = NULL;
    }

    ttak_mem_free(m->tab);
    *m = nm;
    return true;
}

static str_entry_t *str_map_get_or_insert(str_map_t *m, const char *key) {
    if (!m || !key) return NULL;
    if (!m->tab) {
        if (!str_map_init(m, 4096)) return NULL;
    }
    if ((m->len + 1) * 10 >= m->cap * 7) {
        if (!str_map_rehash(m, m->cap * 2)) return NULL;
    }

    size_t mask = m->cap - 1;
    size_t i = (size_t)hash_str64(key) & mask;
    while (m->tab[i].key) {
        if (strcmp(m->tab[i].key, key) == 0) return &m->tab[i];
        i = (i + 1) & mask;
    }

    size_t klen = strlen(key);
    char *dup = (char *)ttak_mem_alloc(klen + 1, __TTAK_UNSAFE_MEM_FOREVER__, monotonic_millis());
    if (!dup) return NULL;
    memcpy(dup, key, klen + 1);

    m->tab[i].key = dup;
    m->tab[i].state = 0;
    m->len++;
    return &m->tab[i];
}

static str_entry_t *str_map_lookup(str_map_t *m, const char *key) {
    if (!m || !m->tab || !key) return NULL;
    size_t mask = m->cap - 1;
    size_t i = (size_t)hash_str64(key) & mask;
    while (m->tab[i].key) {
        if (strcmp(m->tab[i].key, key) == 0) return &m->tab[i];
        i = (i + 1) & mask;
    }
    return NULL;
}

static str_map_t g_block_state;
static str_map_t g_inflight_state;

/* -------------------------------------------------------------------------- */
/* Requeue stack (BigInt-first)                                                 */
/* -------------------------------------------------------------------------- */

typedef struct seed_node_t {
    core_seed_t v;
    struct seed_node_t *next;
} seed_node_t;

static seed_node_t *g_requeue_head = NULL;

static bool requeue_push_copy(const core_seed_t *v, uint64_t now) {
    seed_node_t *n = (seed_node_t *)ttak_mem_alloc(sizeof(seed_node_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!n) return false;
    core_seed_init_zero(&n->v, now);
    if (!ttak_bigint_copy(&n->v.big, &v->big, now)) {
        core_seed_free(&n->v, now);
        ttak_mem_free(n);
        return false;
    }
    (void)core_seed_refresh_u64_cache(&n->v, now);
    n->next = g_requeue_head;
    g_requeue_head = n;
    return true;
}

static bool requeue_pop_move(core_seed_t *out, uint64_t now) {
    (void)now;
    if (!g_requeue_head || !out) return false;
    seed_node_t *n = g_requeue_head;
    g_requeue_head = n->next;
    core_seed_free(out, now);
    *out = n->v; /* move */
    ttak_mem_free(n);
    return true;
}

static void requeue_free_all(uint64_t now) {
    while (g_requeue_head) {
        seed_node_t *n = g_requeue_head;
        g_requeue_head = n->next;
        core_seed_free(&n->v, now);
        ttak_mem_free(n);
    }
}

/* -------------------------------------------------------------------------- */
/* Checkpoint (best effort, BigInt-first)                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Writes checkpoint values to a temp file and renames atomically.
 * @param verified Verified frontier.
 * @param next Next dispatch.
 * @param now Monotonic timestamp.
 */
static void save_checkpoint(const core_seed_t *verified, const core_seed_t *next, uint64_t now) {
    /* Defensive: ensure we never record next < verified in checkpoint */
    const core_seed_t *n_val = next;
    if (safe_core_seed_cmp(next, verified) < 0) {
        n_val = verified;
    }

    char *v = core_seed_to_decimal(verified, now);
    char *n = core_seed_to_decimal(n_val, now);
    if (!v || !n) {
        if (v) ttak_mem_free(v);
        if (n) ttak_mem_free(n);
        return;
    }

    char tmp_path[4104];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_checkpoint_path);

    int fd = open(tmp_path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        ttak_mem_free(v);
        ttak_mem_free(n);
        return;
    }

    char buf[4096];
    int w = snprintf(buf, sizeof(buf), "verified=%s\nnext=%s\nscript_sha256=%s\n", v, n, g_script_hash_hex);
    bool ok = (w > 0 && (size_t)w < sizeof(buf)) ? write_all(fd, buf, (size_t)w) : false;
    if (ok) (void)fsync(fd);
    close(fd);

    if (!ok) {
        remove(tmp_path);
    } else {
        if (rename(tmp_path, g_checkpoint_path) != 0) remove(tmp_path);
    }

    ttak_mem_free(v);
    ttak_mem_free(n);
}

/**
 * @brief Loads checkpoint values (BigInt-first). Missing checkpoint is allowed.
 * @param verified Output verified frontier.
 * @param next Output next dispatch.
 * @param now Monotonic timestamp.
 * @return true on success, false on malformed checkpoint.
 */
static bool load_checkpoint(core_seed_t *verified, core_seed_t *next, uint64_t now) {
    core_seed_init_zero(verified, now);
    core_seed_init_zero(next, now);

    FILE *fp = fopen(g_checkpoint_path, "r");
    if (!fp) return true;

    char line[4096];
    bool got_v = false, got_n = false;

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';

        if (strncmp(line, "verified=", 9) == 0) {
            const char *s = line + 9;
            if (!core_seed_parse_decimal(verified, s, now)) {
                fclose(fp);
                return false;
            }
            got_v = true;
        } else if (strncmp(line, "next=", 5) == 0) {
            const char *s = line + 5;
            if (!core_seed_parse_decimal(next, s, now)) {
                fclose(fp);
                return false;
            }
            got_n = true;
        } else if (strncmp(line, "script_sha256=", 14) == 0) {
            strncpy(g_resume_script_hash_hex, line + 14, 64);
            g_resume_script_hash_hex[64] = '\0';
        }
    }

    fclose(fp);
    return got_v && got_n;
}

/* -------------------------------------------------------------------------- */
/* Journal                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Parses a journal line.
 *
 * Formats:
 * - Reserve: R <task_id> <range_start_decimal> <count>
 * - Done:    D <task_id> <range_start_decimal> <count> <sha256hex64>
 *
 * @param line Input line (newline allowed).
 * @param type_out Output 'R' or 'D'.
 * @param task_id_out Output task id.
 * @param start_out Output range_start decimal string buffer.
 * @param count_out Output count.
 * @param hash_hex_out Output hash hex (only for 'D').
 * @return true on success, false on failure.
 */
static bool parse_journal_line(const char *line,
                               char *type_out,
                               uint64_t *task_id_out,
                               char start_out[2048],
                               uint64_t *count_out,
                               char hash_hex_out[65]) {
    if (!line || !*line) return false;

    char type = 0;
    uint64_t tid = 0, count = 0;
    char start_buf[2048];
    char hash_buf[80];

    memset(start_buf, 0, sizeof(start_buf));
    memset(hash_buf, 0, sizeof(hash_buf));

    if (line[0] == 'R') {
        if (sscanf(line, "R %" SCNu64 " %2047s %" SCNu64, &tid, start_buf, &count) != 3) return false;
        type = 'R';
        if (!is_canonical_decimal(start_buf)) return false;
    } else if (line[0] == 'D') {
        if (sscanf(line, "D %" SCNu64 " %2047s %" SCNu64 " %64s", &tid, start_buf, &count, hash_buf) != 4) return false;
        type = 'D';
        if (!is_canonical_decimal(start_buf)) return false;
        if (strlen(hash_buf) != 64) return false;
        for (size_t i = 0; i < 64; i++) {
            char c = hash_buf[i];
            bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            if (!ok) return false;
        }
    } else {
        return false;
    }

    if (count != BLOCK_SIZE) return false;

    if (type_out) *type_out = type;
    if (task_id_out) *task_id_out = tid;
    if (count_out) *count_out = count;
    if (start_out) {
        strncpy(start_out, start_buf, 2047);
        start_out[2047] = '\0';
    }
    if (hash_hex_out) {
        if (type == 'D') {
            memcpy(hash_hex_out, hash_buf, 65);
            hash_hex_out[64] = '\0';
        } else {
            hash_hex_out[0] = '\0';
        }
    }
    return true;
}

/**
 * @brief Appends a RESERVE record to journal (durable).
 * @param task_id Task id.
 * @param start Range start.
 * @param count Range size (must be BLOCK_SIZE).
 * @param now Monotonic timestamp.
 * @return true on success, false on failure.
 */
static bool journal_append_reserve(uint64_t task_id, const core_seed_t *start, uint64_t count, uint64_t now) {
    char *start_s = core_seed_to_decimal(start, now);
    if (!start_s) return false;

    char buf[4096];
    int n = snprintf(buf, sizeof(buf), "R %" PRIu64 " %s %" PRIu64 "\n", task_id, start_s, count);
    ttak_mem_free(start_s);
    if (n <= 0 || (size_t)n >= sizeof(buf)) return false;

    pthread_mutex_lock(&g_journal_lock);
    bool ok = append_line_fsync(g_journal_path, buf);
    pthread_mutex_unlock(&g_journal_lock);
    return ok;
}

/**
 * @brief Appends a DONE record to journal (durable).
 * @param task_id Task id.
 * @param start Range start.
 * @param count Range size (must be BLOCK_SIZE).
 * @param hash_hex Lowercase hex SHA-256 string (64 chars).
 * @param now Monotonic timestamp.
 * @return true on success, false on failure.
 */
static bool journal_append_done(uint64_t task_id, const core_seed_t *start, uint64_t count, const char *hash_hex, uint64_t now) {
    if (!hash_hex) return false;

    char *start_s = core_seed_to_decimal(start, now);
    if (!start_s) return false;

    char buf[4096];
    int n = snprintf(buf, sizeof(buf), "D %" PRIu64 " %s %" PRIu64 " %s\n", task_id, start_s, count, hash_hex);
    ttak_mem_free(start_s);
    if (n <= 0 || (size_t)n >= sizeof(buf)) return false;

    pthread_mutex_lock(&g_journal_lock);
    bool ok = append_line_fsync(g_journal_path, buf);
    pthread_mutex_unlock(&g_journal_lock);
    return ok;
}

/* -------------------------------------------------------------------------- */
/* Derived logs                                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Logs a found seed as JSONL (best effort).
 * @param seed BigInt seed value.
 * @param now Monotonic timestamp.
 */
static void log_found_seed_bigint(const ttak_bigint_t *seed, uint64_t now) {
    char *s = ttak_bigint_to_string(seed, now);
    if (!s) return;

    char buf[4096];
    int n;
    if (g_script_hash_hex[0] != '\0') {
        n = snprintf(buf, sizeof(buf),
                     "{\"status\":\"found\",\"seed\":\"%s\",\"ts\":%" PRIu64 ",\"script_sha256\":\"%s\"}\n",
                     s, (uint64_t)time(NULL), g_script_hash_hex);
    } else {
        n = snprintf(buf, sizeof(buf),
                     "{\"status\":\"found\",\"seed\":\"%s\",\"ts\":%" PRIu64 "}\n",
                     s, (uint64_t)time(NULL));
    }
    ttak_mem_free(s);
    if (n <= 0 || (size_t)n >= sizeof(buf)) return;

    pthread_mutex_lock(&g_log_lock);
    (void)append_line_best_effort(g_found_log_path, buf);
    pthread_mutex_unlock(&g_log_lock);
}

/**
 * @brief Logs a range proof as JSONL (best effort).
 * @param range_start Range start.
 * @param count Range size.
 * @param hash_hex SHA-256 proof hex string.
 * @param now Monotonic timestamp.
 */
static void log_proof_core_seed(const core_seed_t *range_start, uint64_t count, const char *hash_hex, uint64_t now) {
    char *s = core_seed_to_decimal(range_start, now);
    if (!s) return;

    char buf[4096];
    int n = snprintf(buf, sizeof(buf),
                     "{\"range_start\":\"%s\",\"count\":%" PRIu64 ",\"proof_sha256\":\"%s\",\"ts\":%" PRIu64 "}\n",
                     s, count, hash_hex, (uint64_t)time(NULL));
    ttak_mem_free(s);
    if (n <= 0 || (size_t)n >= sizeof(buf)) return;

    pthread_mutex_lock(&g_log_lock);
    (void)append_line_best_effort(g_hash_log_path, buf);
    pthread_mutex_unlock(&g_log_lock);
}

/* -------------------------------------------------------------------------- */
/* Recovery: journal authoritative                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Replays journal and reconstructs dispatch/verified state.
 *
 * Rebuilds:
 * - g_block_state: reserved/done keyed by range_start string
 * - requeue list: reserved-but-not-done starts
 * - g_next_task_id: max(task_id)+1
 * - g_next_dispatch: max(range_start + count) observed in journal
 * - g_verified_frontier: contiguous DONE frontier from 0 (step BLOCK_SIZE)
 *
 * Checkpoint is used as a baseline if present.
 *
 * Any parsing failure is fatal.
 *
 * @param now Monotonic timestamp.
 */
static void journal_recover_or_init(uint64_t now) {
    core_seed_t ckpt_v, ckpt_n;
    core_seed_init_zero(&ckpt_v, now);
    core_seed_init_zero(&ckpt_n, now);

    if (load_checkpoint(&ckpt_v, &ckpt_n, now)) {
        if (safe_core_seed_cmp(&ckpt_n, &ckpt_v) < 0) {
            (void)core_seed_copy(&ckpt_n, &ckpt_v, now);
        }
        (void)core_seed_copy(&g_verified_frontier, &ckpt_v, now);
        (void)core_seed_copy(&g_next_dispatch, &ckpt_n, now);
    }

    FILE *fp = fopen(g_journal_path, "r");
    if (fp) {
        if (!g_block_state.tab) str_map_init(&g_block_state, 16384);

        uint64_t max_tid = 0;
        core_seed_t j_max_end;
        core_seed_init_zero(&j_max_end, now);

        char line[4096];
        while (fgets(line, sizeof(line), fp)) {
            char type; uint64_t tid, count;
            char start_s[2048]; char hash[65];
            if (!parse_journal_line(line, &type, &tid, start_s, &count, hash)) continue;

            if (tid > max_tid) max_tid = tid;
            str_entry_t *e = str_map_get_or_insert(&g_block_state, start_s);
            if (e) e->state = (type == 'R') ? 1 : 2;

            core_seed_t sv, ev;
            core_seed_init_zero(&sv, now); core_seed_init_zero(&ev, now);
            if (core_seed_parse_decimal(&sv, start_s, now)) {
                if (core_seed_add_u64(&ev, &sv, count, now)) {
                    if (safe_core_seed_cmp(&ev, &j_max_end) > 0) {
                        (void)core_seed_copy(&j_max_end, &ev, now);
                    }
                }
            }
            core_seed_free(&sv, now); core_seed_free(&ev, now);
        }
        fclose(fp);

        if (max_tid >= g_next_task_id) g_next_task_id = max_tid + 1;
        if (safe_core_seed_cmp(&j_max_end, &g_next_dispatch) > 0) {
            (void)core_seed_copy(&g_next_dispatch, &j_max_end, now);
        }
        core_seed_free(&j_max_end, now);

        /* Rebuild requeue */
        for (size_t i = 0; i < g_block_state.cap; i++) {
            if (g_block_state.tab[i].key && g_block_state.tab[i].state == 1) {
                core_seed_t tmp; core_seed_init_zero(&tmp, now);
                if (core_seed_parse_decimal(&tmp, g_block_state.tab[i].key, now)) {
                    requeue_push_copy(&tmp, now);
                }
                core_seed_free(&tmp, now);
            }
        }

        /* Recompute verified frontier */
        core_seed_t cursor; core_seed_init_zero(&cursor, now);
        for (;;) {
            char *cstr = core_seed_to_decimal(&cursor, now);
            str_entry_t *e = str_map_lookup(&g_block_state, cstr);
            ttak_mem_free(cstr);
            if (!e || e->state != 2) break;
            if (!core_seed_add_block_inplace(&cursor, now)) break;
        }
        if (safe_core_seed_cmp(&cursor, &g_verified_frontier) > 0) {
            (void)core_seed_copy(&g_verified_frontier, &cursor, now);
        }
        core_seed_free(&cursor, now);
    }

    /* Absolute Final Sync: Next must not be behind Verified */
    if (safe_core_seed_cmp(&g_next_dispatch, &g_verified_frontier) < 0) {
        (void)core_seed_copy(&g_next_dispatch, &g_verified_frontier, now);
    }

    core_seed_free(&ckpt_v, now); core_seed_free(&ckpt_n, now);
    save_checkpoint(&g_verified_frontier, &g_next_dispatch, now);
}

/* -------------------------------------------------------------------------- */
/* Shutdown                                                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Watchdog thread that forces process exit if shutdown blocks too long.
 * @param arg Unused.
 * @return NULL.
 */
static void *shutdown_watchdog(void *arg) {
    (void)arg;
    struct timespec ts = { .tv_sec = SHUTDOWN_TIMEOUT_S, .tv_nsec = 0 };
    nanosleep(&ts, NULL);

    if (ttak_atomic_read64(&g_shutdown_requested)) {
        fprintf(stderr, "\n[FATAL] Shutdown timed out after %d seconds. Forcing exit.\n", SHUTDOWN_TIMEOUT_S);
        _exit(EXIT_FAILURE);
    }
    return NULL;
}

/**
 * @brief Signal handler initiating graceful shutdown; second signal forces hard exit.
 * @param sig Signal number.
 */
static void handle_signal(int sig) {
    (void)sig;

    if (ttak_atomic_read64(&g_shutdown_requested)) {
        ttak_atomic_write64(&g_hard_shutdown_requested, 1);
        fprintf(stderr, "\n[FATAL] Hard shutdown requested. Forcing exit.\n");
        _exit(EXIT_FAILURE);
    }

    ttak_atomic_write64(&g_shutdown_requested, 1);

    static int watchdog_started = 0;
    if (!watchdog_started) {
        watchdog_started = 1;
        pthread_t tid;
        if (pthread_create(&tid, NULL, shutdown_watchdog, NULL) == 0) {
            pthread_detach(tid);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Worker                                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Worker routine scanning a task range.
 *
 * The task start is BigInt-first and is used to initialize a BigInt cursor.
 * For each seed in the range:
 * - compute sum-of-proper-divisors chain up to 3 steps
 * - report a period-3 cycle candidate when appropriate
 * - update progress counters
 *
 * Any arithmetic failure triggers an immediate process exit, because partial/dangling state
 * would make resume correctness ambiguous.
 *
 * @param arg scan_task_t pointer.
 * @return NULL.
 */
static void *worker_scan_range(void *arg) {
    scan_task_t *task = (scan_task_t *)arg;
    uint64_t now = monotonic_millis();

    SHA256_CTX sha_ctx;
    sha256_init(&sha_ctx);

    ttak_bigint_t seed_val, s1, s2, s3;
    ttak_bigint_init_u64(&seed_val, 0, now);
    ttak_bigint_init_u64(&s1, 0, now);
    ttak_bigint_init_u64(&s2, 0, now);
    ttak_bigint_init_u64(&s3, 0, now);

    if (!ttak_bigint_copy(&seed_val, &task->start.big, now)) {
        fprintf(stderr, "[FATAL] BigInt copy failed in worker.\n");
        _exit(EXIT_FAILURE);
    }

    uint64_t processed = 0;
    uint64_t pending_flush = 0;

    ttak_bigscript_vm_t *vm = NULL;
    if (g_script_prog) {
        ttak_bigscript_limits_t lim = {
            .max_steps_per_seed = 10000,
            .max_stack_words = 1024,
            .max_call_depth = 32
        };
        vm = ttak_bigscript_vm_create(&lim, now);
    }

    for (uint64_t i = 0; i < task->count; i++) {
        if (ttak_atomic_read64(&g_shutdown_requested)) break;

        char *seed_s = ttak_bigint_to_string(&seed_val, now);
        if (!seed_s) {
            fprintf(stderr, "[FATAL] BigInt stringify failed in worker.\n");
            _exit(EXIT_FAILURE);
        }
        sha256_update(&sha_ctx, (const uint8_t *)seed_s, strlen(seed_s));
        ttak_mem_free(seed_s);

        if (!ttak_sum_proper_divisors_big(&seed_val, &s1, now)) {
            fprintf(stderr, "[FATAL] sum_proper_divisors failed.\n");
            _exit(EXIT_FAILURE);
        }

        char *s1_s = ttak_bigint_to_string(&s1, now);
        if (!s1_s) {
            fprintf(stderr, "[FATAL] BigInt stringify failed in worker.\n");
            _exit(EXIT_FAILURE);
        }
        sha256_update(&sha_ctx, (const uint8_t *)s1_s, strlen(s1_s));
        ttak_mem_free(s1_s);

        if (g_script_prog) {
            if (!vm) {
                fprintf(stderr, "[FATAL] VM creation failed in worker.\n");
                _exit(EXIT_FAILURE);
            }
            ttak_bigscript_value_t out_val;
            memset(&out_val, 0, sizeof(out_val));
            ttak_bigscript_error_t err = {0};
            if (ttak_bigscript_eval_seed(g_script_prog, vm, &seed_val, &s1, &out_val, &err, now)) {
                if (out_val.is_found) {
                    log_found_seed_bigint(&seed_val, now);
                }
                ttak_bigscript_value_free(&out_val, now);
            } else {
                fprintf(stderr, "[FATAL] eval_seed failed: %s\n", err.message ? err.message : "Unknown");
                _exit(EXIT_FAILURE);
            }
        } else {
            if (ttak_bigint_cmp(&s1, &seed_val) != 0 && ttak_bigint_cmp_u64(&s1, 1) > 0) {
                if (!ttak_sum_proper_divisors_big(&s1, &s2, now)) {
                    fprintf(stderr, "[FATAL] sum_proper_divisors failed.\n");
                    _exit(EXIT_FAILURE);
                }
                if (ttak_bigint_cmp(&s2, &seed_val) != 0) {
                    if (!ttak_sum_proper_divisors_big(&s2, &s3, now)) {
                        fprintf(stderr, "[FATAL] sum_proper_divisors failed.\n");
                        _exit(EXIT_FAILURE);
                    }
                    if (ttak_bigint_cmp(&s3, &seed_val) == 0) {
                        if (ttak_bigint_cmp(&seed_val, &s1) < 0 && ttak_bigint_cmp(&seed_val, &s2) < 0) {
                            log_found_seed_bigint(&seed_val, now);
                        }
                    }
                }
            }
        }

        if (!ttak_bigint_add_u64(&seed_val, &seed_val, 1, now)) {
            fprintf(stderr, "[FATAL] BigInt add failed.\n");
            _exit(EXIT_FAILURE);
        }

        processed++;
        pending_flush++;
        if (pending_flush >= PROGRESS_FLUSH_STRIDE) {
            ttak_atomic_add64(&g_total_scanned, pending_flush);
            pending_flush = 0;
        }
    }

    if (vm) ttak_bigscript_vm_free(vm, now);

    if (pending_flush) {
        ttak_atomic_add64(&g_total_scanned, pending_flush);
    }

    bool completed = (processed == task->count && !ttak_atomic_read64(&g_shutdown_requested));

    if (completed) {
        uint8_t digest[32];
        sha256_final(&sha_ctx, digest);

        char hash_hex[65];
        for (size_t j = 0; j < sizeof(digest); j++) {
            snprintf(hash_hex + (j * 2), 3, "%02x", digest[j]);
        }
        hash_hex[64] = '\0';

        if (!journal_append_done(task->task_id, &task->start, task->count, hash_hex, now)) {
            fprintf(stderr, "[FATAL] Failed to append DONE to journal.\n");
            _exit(EXIT_FAILURE);
        }

        log_proof_core_seed(&task->start, task->count, hash_hex, now);

        char *start_s = core_seed_to_decimal(&task->start, now);
        if (!start_s) {
            fprintf(stderr, "[FATAL] Conversion failed after completion.\n");
            _exit(EXIT_FAILURE);
        }

        pthread_mutex_lock(&g_state_lock);
        str_entry_t *e = str_map_get_or_insert(&g_block_state, start_s);
        if (!e) {
            pthread_mutex_unlock(&g_state_lock);
            ttak_mem_free(start_s);
            fprintf(stderr, "[FATAL] Block state allocation failure.\n");
            _exit(EXIT_FAILURE);
        }
        e->state = 2;

        str_entry_t *ie = str_map_lookup(&g_inflight_state, start_s);
        if (ie) ie->state = 0;
        pthread_mutex_unlock(&g_state_lock);

        ttak_mem_free(start_s);
    }

    ttak_bigint_free(&seed_val, now);
    ttak_bigint_free(&s1, now);
    ttak_bigint_free(&s2, now);
    ttak_bigint_free(&s3, now);

    core_seed_free(&task->start, now);
    ttak_mem_free(task);

    ttak_atomic_add64(&g_inflight, (uint64_t)(-1LL));
    return NULL;
}

/* -------------------------------------------------------------------------- */
/* Dispatch                                                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Dispatches a single task if it is not already done/inflight.
 * @param pool Thread pool.
 * @param range_start Range start.
 * @param count Range size.
 * @param now Monotonic timestamp.
 * @return true on success, false on failure.
 */
static bool dispatch_one(ttak_thread_pool_t *pool, const core_seed_t *range_start, uint64_t count, uint64_t now) {
    if (!pool || !range_start) return false;
    if (count != BLOCK_SIZE) return false;

    char *start_s = core_seed_to_decimal(range_start, now);
    if (!start_s) return false;

    pthread_mutex_lock(&g_state_lock);

    str_entry_t *done = str_map_lookup(&g_block_state, start_s);
    if (done && done->state == 2) {
        pthread_mutex_unlock(&g_state_lock);
        ttak_mem_free(start_s);
        return true;
    }

    str_entry_t *in = str_map_lookup(&g_inflight_state, start_s);
    if (in && in->state == 1) {
        pthread_mutex_unlock(&g_state_lock);
        ttak_mem_free(start_s);
        return true;
    }

    str_entry_t *ins = str_map_get_or_insert(&g_inflight_state, start_s);
    if (!ins) {
        pthread_mutex_unlock(&g_state_lock);
        ttak_mem_free(start_s);
        return false;
    }
    ins->state = 1;

    pthread_mutex_unlock(&g_state_lock);

    scan_task_t *task = (scan_task_t *)ttak_mem_alloc(sizeof(scan_task_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!task) {
        pthread_mutex_lock(&g_state_lock);
        str_entry_t *rb = str_map_lookup(&g_inflight_state, start_s);
        if (rb) rb->state = 0;
        pthread_mutex_unlock(&g_state_lock);
        ttak_mem_free(start_s);
        return false;
    }

    task->task_id = g_next_task_id++;
    task->count = count;
    core_seed_init_zero(&task->start, now);
    if (!ttak_bigint_copy(&task->start.big, &range_start->big, now)) {
        core_seed_free(&task->start, now);
        ttak_mem_free(task);
        ttak_mem_free(start_s);
        return false;
    }
    (void)core_seed_refresh_u64_cache(&task->start, now);

    if (!journal_append_reserve(task->task_id, &task->start, task->count, now)) {
        fprintf(stderr, "[FATAL] Failed to append RESERVE to journal.\n");
        _exit(EXIT_FAILURE);
    }

    pthread_mutex_lock(&g_state_lock);
    str_entry_t *be = str_map_get_or_insert(&g_block_state, start_s);
    if (be && be->state == 0) be->state = 1;
    pthread_mutex_unlock(&g_state_lock);

    ttak_mem_free(start_s);

    ttak_atomic_add64(&g_inflight, 1);

    if (!ttak_thread_pool_submit_task(pool, worker_scan_range, task, 0, monotonic_millis())) {
        worker_scan_range(task);
    }
    return true;
}

/* -------------------------------------------------------------------------- */
/* Verified frontier advancement                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Advances verified frontier using contiguous DONE blocks from current frontier.
 * @param now Monotonic timestamp.
 */
static void advance_verified_frontier(uint64_t now) {
    core_seed_t cursor;
    core_seed_init_zero(&cursor, now);
    (void)core_seed_copy(&cursor, &g_verified_frontier, now);

    for (;;) {
        char *cstr = core_seed_to_decimal(&cursor, now);
        if (!cstr) {
            core_seed_free(&cursor, now);
            fprintf(stderr, "[FATAL] Conversion failed while advancing verified frontier.\n");
            _exit(EXIT_FAILURE);
        }

        str_entry_t *e = str_map_lookup(&g_block_state, cstr);
        ttak_mem_free(cstr);

        if (!e || e->state != 2) break;

        if (!core_seed_add_block_inplace(&cursor, now)) {
            core_seed_free(&cursor, now);
            fprintf(stderr, "[FATAL] Arithmetic failure while advancing verified frontier.\n");
            _exit(EXIT_FAILURE);
        }
    }

    (void)core_seed_copy(&g_verified_frontier, &cursor, now);
    core_seed_free(&cursor, now);
}

/* -------------------------------------------------------------------------- */
/* main                                                                         */
/* -------------------------------------------------------------------------- */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    uint64_t now = monotonic_millis();

    configure_state_paths();
    acquire_single_instance_lock();

    if (!str_map_init(&g_block_state, 16384) || !str_map_init(&g_inflight_state, 8192)) {
        fprintf(stderr, "[FATAL] Map allocation failure.\n");
        return EXIT_FAILURE;
    }

    core_seed_init_zero(&g_next_dispatch, now);
    core_seed_init_zero(&g_verified_frontier, now);

    const char *script_path = getenv("ALIQUOT_SCRIPT_PATH");
    const char *script_text = getenv("ALIQUOT_SCRIPT_TEXT");
    char *loaded_script = NULL;

    if (script_path) {
        FILE *fp = fopen(script_path, "r");
        if (!fp) {
            fprintf(stderr, "[FATAL] Failed to open script: %s\n", script_path);
            return EXIT_FAILURE;
        }
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        loaded_script = (char*)malloc(sz + 1);
        fread(loaded_script, 1, sz, fp);
        loaded_script[sz] = '\0';
        fclose(fp);
    } else if (script_text) {
        loaded_script = strdup(script_text);
    }

    if (loaded_script) {
        ttak_bigscript_limits_t limits = {
            .max_tokens = 100000,
            .max_ast_nodes = 100000,
            .max_call_depth = 32,
            .max_stack_words = 1024,
            .max_steps_per_seed = 10000
        };
        ttak_bigscript_error_t err = {0};
        g_script_prog = ttak_bigscript_compile(loaded_script, NULL, &limits, &err, now);
        if (!g_script_prog) {
            fprintf(stderr, "[FATAL] Script compile error: %s\n", err.message ? err.message : "Unknown");
            return EXIT_FAILURE;
        }
        ttak_bigscript_hash_program(g_script_prog, g_script_hash_hex);
        free(loaded_script);
    }

    journal_recover_or_init(now);

    const char *env_start_seed = getenv("ALIQUOT_START_SEED");
    if (env_start_seed && *env_start_seed) {
        core_seed_t forced_start;
        core_seed_init_zero(&forced_start, now);
        if (core_seed_parse_decimal(&forced_start, env_start_seed, now)) {
            printf("[SYSTEM] Manually overriding next dispatch seed to: %s\n", env_start_seed);
            (void)core_seed_copy(&g_next_dispatch, &forced_start, now);
        } else {
            fprintf(stderr, "[WARNING] Invalid ALIQUOT_START_SEED: %s. Ignoring override.\n", env_start_seed);
        }
        core_seed_free(&forced_start, now);
    }

    if (g_resume_script_hash_hex[0] != '\0') {
        if (strcmp(g_script_hash_hex, g_resume_script_hash_hex) != 0) {
            const char *allow_mismatch = getenv("ALIQUOT_SCRIPT_ALLOW_MISMATCH");
            if (!(allow_mismatch && strcmp(allow_mismatch, "1") == 0)) {
                fprintf(stderr, "[FATAL] Script hash mismatch during resume. Expected %s but got %s.\n",
                        g_resume_script_hash_hex, g_script_hash_hex[0] ? g_script_hash_hex : "(none)");
                fprintf(stderr, "        Refusing to start. Set ALIQUOT_SCRIPT_ALLOW_MISMATCH=1 to override.\n");
                return EXIT_FAILURE;
            }
        }
    }

    if (ttak_bigint_is_zero(&g_verified_frontier.big)) {
        struct stat stj;
        if (stat(g_journal_path, &stj) == 0 && stj.st_size > 0) {
            const char *allow = getenv("ALIQUOT_ALLOW_RESET");
            if (!(allow && strcmp(allow, "1") == 0)) {
                fprintf(stderr, "[FATAL] Resume computed verified frontier 0 while journal is non-empty.\n");
                fprintf(stderr, "        Refusing to start from 0 to avoid duplicate scanning.\n");
                fprintf(stderr, "        Fix state corruption or set ALIQUOT_ALLOW_RESET=1 to override.\n");
                return EXIT_FAILURE;
            }
        }
    }

    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus < 1) cpus = 1;

    uint64_t max_inflight = (uint64_t)cpus * (uint64_t)MAX_INFLIGHT_FACTOR;
    if (max_inflight < 1) max_inflight = 1;

    ttak_thread_pool_t *pool = ttak_thread_pool_create(cpus, 0, monotonic_millis());
    if (!pool) {
        fprintf(stderr, "[FATAL] Unable to create thread pool.\n");
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (safe_core_seed_cmp(&g_next_dispatch, &g_verified_frontier) < 0) {
        (void)core_seed_copy(&g_next_dispatch, &g_verified_frontier, now);
    }

    char *vstr = core_seed_to_decimal(&g_verified_frontier, monotonic_millis());
    char *nstr = core_seed_to_decimal(&g_next_dispatch, monotonic_millis());

    printf("[SYSTEM] Aliquot scanner online with %ld worker threads.\n", cpus);
    printf("[SYSTEM] Resuming from verified seed %s\n", vstr ? vstr : "conversion_error");
    printf("[SYSTEM] Next dispatch seed %s\n", nstr ? nstr : "conversion_error");

    if (vstr) ttak_mem_free(vstr);
    if (nstr) ttak_mem_free(nstr);

    uint64_t last_status = monotonic_millis();
    uint64_t last_ckpt = last_status;
    uint64_t last_rate_ts = last_status;
    uint64_t last_rate_total = ttak_atomic_read64(&g_total_scanned);

    while (!ttak_atomic_read64(&g_shutdown_requested)) {
        uint64_t inflight = ttak_atomic_read64(&g_inflight);

        while (inflight < max_inflight && !ttak_atomic_read64(&g_shutdown_requested)) {
            core_seed_t start;
            core_seed_init_zero(&start, now);

            if (requeue_pop_move(&start, now)) {
                if (!dispatch_one(pool, &start, BLOCK_SIZE, now)) {
                    fprintf(stderr, "[FATAL] Dispatch failed.\n");
                    core_seed_free(&start, now);
                    ttak_atomic_write64(&g_shutdown_requested, 1);
                    break;
                }
                core_seed_free(&start, now);
            } else {
                /* Peek if next block is already done or verified to avoid slow spin */
                bool skip_this = false;
                if (safe_core_seed_cmp(&g_next_dispatch, &g_verified_frontier) < 0) {
                    skip_this = true;
                } else {
                    char *nstr_peek = core_seed_to_decimal(&g_next_dispatch, now);
                    if (nstr_peek) {
                        pthread_mutex_lock(&g_state_lock);
                        str_entry_t *e = str_map_lookup(&g_block_state, nstr_peek);
                        if (e && e->state == 2) skip_this = true;
                        pthread_mutex_unlock(&g_state_lock);
                        ttak_mem_free(nstr_peek);
                    }
                }

                if (skip_this) {
                    if (!core_seed_add_block_inplace(&g_next_dispatch, now)) {
                        fprintf(stderr, "[FATAL] Arithmetic failure while advancing next dispatch.\n");
                        core_seed_free(&start, now);
                        _exit(EXIT_FAILURE);
                    }
                    core_seed_free(&start, now);
                    continue; /* Jump to next block immediately without sleeping */
                }

                (void)core_seed_copy(&start, &g_next_dispatch, now);
                if (!core_seed_add_block_inplace(&g_next_dispatch, now)) {
                    fprintf(stderr, "[FATAL] Arithmetic failure while advancing next dispatch.\n");
                    core_seed_free(&start, now);
                    _exit(EXIT_FAILURE);
                }
                if (!dispatch_one(pool, &start, BLOCK_SIZE, now)) {
                    fprintf(stderr, "[FATAL] Dispatch failed.\n");
                    core_seed_free(&start, now);
                    ttak_atomic_write64(&g_shutdown_requested, 1);
                    break;
                }
                core_seed_free(&start, now);
            }
            inflight = ttak_atomic_read64(&g_inflight);
        }

        now = monotonic_millis();


        pthread_mutex_lock(&g_state_lock);
        advance_verified_frontier(now);
        pthread_mutex_unlock(&g_state_lock);

        if (now - last_status >= STATUS_INTERVAL_MS) {
            uint64_t current_total = ttak_atomic_read64(&g_total_scanned);
            double dt = (now - last_rate_ts) / 1000.0;
            double rate = 0.0;
            if (dt > 0.0 && current_total >= last_rate_total) {
                rate = (double)(current_total - last_rate_total) / dt;
            }

            uint64_t inflight_now = ttak_atomic_read64(&g_inflight);

            pthread_mutex_lock(&g_state_lock);
            char *next_s = core_seed_to_decimal(&g_next_dispatch, now);
            char *ver_s  = core_seed_to_decimal(&g_verified_frontier, now);
            pthread_mutex_unlock(&g_state_lock);

            printf("[STATUS] Next %s | Verified %s | Rate %.2f seeds/sec | InFlight %" PRIu64 "/%" PRIu64 "\n",
                   next_s ? next_s : "conversion_error",
                   ver_s ? ver_s : "conversion_error",
                   rate, inflight_now, max_inflight);

            if (next_s) ttak_mem_free(next_s);
            if (ver_s) ttak_mem_free(ver_s);

            last_rate_ts = now;
            last_rate_total = current_total;
            last_status = now;
        }

        if (now - last_ckpt >= CHECKPOINT_INTERVAL_MS) {
            pthread_mutex_lock(&g_state_lock);
            save_checkpoint(&g_verified_frontier, &g_next_dispatch, now);
            pthread_mutex_unlock(&g_state_lock);
            last_ckpt = now;
        }

        struct timespec ts = { .tv_sec = 0, .tv_nsec = DISPATCH_POLL_NS };
        nanosleep(&ts, NULL);
    }

    printf("\n[RETIRE] Shutdown requested. Waiting for workers...\n");

    if (ttak_atomic_read64(&g_hard_shutdown_requested)) {
        fprintf(stderr, "[FATAL] Hard shutdown requested.\n");
        _exit(EXIT_FAILURE);
    }

    pthread_t watchdog_tid;
    if (pthread_create(&watchdog_tid, NULL, shutdown_watchdog, NULL) == 0) {
        pthread_detach(watchdog_tid);
    }

    ttak_thread_pool_destroy(pool);

    now = monotonic_millis();

    pthread_mutex_lock(&g_state_lock);
    advance_verified_frontier(now);
    save_checkpoint(&g_verified_frontier, &g_next_dispatch, now);
    pthread_mutex_unlock(&g_state_lock);

    char *final_v = core_seed_to_decimal(&g_verified_frontier, now);
    if (final_v) {
        printf("[RETIRE] Final verified frontier: %s\n", final_v);
        ttak_mem_free(final_v);
    }

    printf("[RETIRE] Scanner shutdown complete.\n");

    requeue_free_all(now);
    str_map_free(&g_inflight_state);
    str_map_free(&g_block_state);

    core_seed_free(&g_next_dispatch, now);
    core_seed_free(&g_verified_frontier, now);

    if (g_lock_fd >= 0) {
        (void)flock(g_lock_fd, LOCK_UN);
        close(g_lock_fd);
        g_lock_fd = -1;
    }

    return EXIT_SUCCESS;
}
