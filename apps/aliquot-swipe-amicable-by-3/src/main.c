/**
 * @file main.c
 * @brief Deterministic Period-3 Sociable Number Scanner for libttak
 *
 * This implementation performs deterministic aliquot sweeps while producing
 * reproducible SHA-256 proofs for every processed range. Each seed within a
 * task is hashed exactly once at the start of the loop iteration, enabling
 * verify.c to recompute proofs by simply replaying range_start..range_start+count-1.
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
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <ttak/atomic/atomic.h>
#include <ttak/math/bigint.h>
#include <ttak/math/sum_divisors.h>
#include <ttak/mem/mem.h>
#include <ttak/security/sha256.h>
#include <ttak/thread/pool.h>
#include <ttak/timing/timing.h>

#if defined(ENABLE_CUDA) || defined(ENABLE_ROCM) || defined(ENABLE_OPENCL)
#define TTAK_GPU_ACCELERATED 1
#else
#define TTAK_GPU_ACCELERATED 0
#endif

/* ========================================================================== */
/*                            Scanner Configuration                           */
/* ========================================================================== */
#define BLOCK_SIZE          10000ULL
#define DEFAULT_START_SEED  1000ULL
#define STATE_DIR           "/opt/aliquot-3"
#define HASH_LOG_NAME       "range_proofs.log"
#define FOUND_LOG_NAME      "sociable_found.jsonl"
#define CHECKPOINT_FILE     "scanner_checkpoint.txt"
#define CHECKPOINT_INTERVAL 5000ULL
#define SHUTDOWN_TIMEOUT_S  30

/**
 * @brief Immutable description of a scanning assignment dispatched to a worker.
 *
 * Each scan task owns a contiguous `[start, start + count)` window that is both
 * hashed and evaluated sequentially. Workers must free @ref start upon completion.
 */
typedef struct {
    ttak_bigint_t start;
    uint64_t count;
} scan_task_t;

/* ========================================================================== */
/*                             Global Runtime State                           */
/* ========================================================================== */
static volatile uint64_t g_shutdown_requested = 0;
static ttak_bigint_t g_next_range_start;
static ttak_bigint_t g_verified_frontier;
static uint64_t g_total_scanned = 0;
static uint64_t g_progress_quantum = BLOCK_SIZE;

static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;

static char g_hash_log_path[4096];
static char g_found_log_path[4096];
static char g_checkpoint_path[4096];

/* ========================================================================== */
/*                                Utilities                                   */
/* ========================================================================== */
static uint64_t monotonic_millis(void) {
    return ttak_get_tick_count();
}

static void handle_signal(int sig) {
    (void)sig;
    ttak_atomic_write64(&g_shutdown_requested, 1);
}

static bool ttak_bigint_init_from_string(ttak_bigint_t *bi, const char *s, uint64_t now) {
    if (!bi || !s) return false;
    ttak_bigint_init_u64(bi, 0, now);
    bool has_digit = false;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            has_digit = true;
            if (!ttak_bigint_mul_u64(bi, bi, 10, now)) return false;
            if (!ttak_bigint_add_u64(bi, bi, (uint64_t)(*s - '0'), now)) return false;
        }
        s++;
    }
    if (!has_digit) {
        ttak_bigint_free(bi, now);
        return false;
    }
    return true;
}

/**
 * @brief Ensures the persistent storage directory exists and updates log paths.
 */
static void ensure_log_directory(void) {
    struct stat st;
    if (stat(STATE_DIR, &st) != 0) {
        if (mkdir(STATE_DIR, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "[FATAL] Unable to create state directory: %s\n", STATE_DIR);
            exit(EXIT_FAILURE);
        }
    }
    snprintf(g_hash_log_path, sizeof(g_hash_log_path), "%s/%s", STATE_DIR, HASH_LOG_NAME);
    snprintf(g_found_log_path, sizeof(g_found_log_path), "%s/%s", STATE_DIR, FOUND_LOG_NAME);
    snprintf(g_checkpoint_path, sizeof(g_checkpoint_path), "%s/%s", STATE_DIR, CHECKPOINT_FILE);
}

/**
 * @brief Calibrates the reporting quantum based on env overrides and accelerator flags.
 */
static void configure_progress_quantum(void) {
    uint64_t quant = BLOCK_SIZE;
#if defined(ENABLE_CUDA) || defined(ENABLE_ROCM) || defined(ENABLE_OPENCL)
    quant = BLOCK_SIZE / 32ULL;
    if (quant == 0) quant = 1;
#endif
    const char *override = getenv("ALIQUOT_RATE_QUANTUM");
    if (override && *override) {
        char *endp = NULL;
        unsigned long long parsed = strtoull(override, &endp, 10);
        if (endp && *endp == '\0' && parsed > 0) {
            if (parsed > BLOCK_SIZE) parsed = BLOCK_SIZE;
            quant = (uint64_t)parsed;
        }
    }
    if (quant == 0 || quant > BLOCK_SIZE) {
        quant = BLOCK_SIZE;
    }
    g_progress_quantum = quant;
}

/**
 * @brief Forces newline termination on log files in case of truncation or crashes.
 */
static void ensure_log_newline(const char *path) {
    if (!path || !*path) return;
    FILE *fp = fopen(path, "ab+");
    if (!fp) return;

    if (fseek(fp, 0, SEEK_END) == 0) {
        long sz = ftell(fp);
        if (sz > 0) {
            if (fseek(fp, -1L, SEEK_END) == 0) {
                int ch = fgetc(fp);
                if (ch != '\n' && ch != EOF) {
                    fseek(fp, 0, SEEK_END);
                    fputc('\n', fp);
                }
            }
        }
    }
    fclose(fp);
}

/**
 * @brief Guards log integrity during shutdown by normalizing trailing newlines.
 */
static void sanitize_logs(void) {
    pthread_mutex_lock(&g_log_lock);
    ensure_log_newline(g_hash_log_path);
    ensure_log_newline(g_found_log_path);
    pthread_mutex_unlock(&g_log_lock);
    printf("[INTEGRITY] Log channels sanitized.\n");
}

/**
 * @brief Restores the scanner frontier from the checkpoint if present.
 */
static void load_checkpoint(uint64_t now) {
    FILE *fp = fopen(g_checkpoint_path, "r");
    if (!fp) {
        ttak_bigint_set_u64(&g_next_range_start, DEFAULT_START_SEED, now);
        ttak_bigint_set_u64(&g_verified_frontier, DEFAULT_START_SEED, now);
        return;
    }

    char buffer[8192];
    bool loaded = false;
    if (fgets(buffer, sizeof(buffer), fp)) {
        size_t len = strlen(buffer);
        while (len && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
            buffer[--len] = '\0';
        }
        ttak_bigint_t parsed;
        if (ttak_bigint_init_from_string(&parsed, buffer, now)) {
            if (!ttak_bigint_is_zero(&parsed)) {
                bool copied_next = ttak_bigint_copy(&g_next_range_start, &parsed, now);
                bool copied_verified = ttak_bigint_copy(&g_verified_frontier, &parsed, now);
                loaded = copied_next && copied_verified;
            }
            ttak_bigint_free(&parsed, now);
        }
    }
    fclose(fp);

    if (!loaded) {
        ttak_bigint_set_u64(&g_next_range_start, DEFAULT_START_SEED, now);
        ttak_bigint_set_u64(&g_verified_frontier, DEFAULT_START_SEED, now);
    }
}

/**
 * @brief Persists the current scanning frontier to disk using an atomic
 *        write (temp file + rename) so a crash between open and write can
 *        never leave a truncated or empty checkpoint behind.
 */
static void save_checkpoint(const ttak_bigint_t *value, uint64_t now) {
    if (!value || ttak_bigint_is_zero(value)) return;
    /* g_checkpoint_path is 4096 bytes; ".tmp" adds 4 more → 4100 + 1 null = 4101 */
    char tmp_path[4104];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_checkpoint_path);
    FILE *fp = fopen(tmp_path, "w");
    if (!fp) return;
    char *repr = ttak_bigint_to_string(value, now);
    bool write_ok = false;
    if (repr) {
        write_ok = (fprintf(fp, "%s\n", repr) > 0);
        ttak_mem_free(repr);
    }
    fflush(fp);
    fclose(fp);
    if (write_ok) {
        rename(tmp_path, g_checkpoint_path);
    } else {
        remove(tmp_path);
    }
}

/**
 * @brief Captures the current frontier for telemetry or checkpointing.
 */
static bool snapshot_next_range_start(ttak_bigint_t *dst, uint64_t now) {
    if (!dst) return false;
    bool copied = false;
    pthread_mutex_lock(&g_state_lock);
    copied = ttak_bigint_copy(dst, &g_next_range_start, now);
    pthread_mutex_unlock(&g_state_lock);
    return copied;
}

/**
 * @brief Captures the last fully verified frontier for logging/checkpoints.
 */
static bool snapshot_verified_frontier(ttak_bigint_t *dst, uint64_t now) {
    if (!dst) return false;
    bool copied = false;
    pthread_mutex_lock(&g_state_lock);
    copied = ttak_bigint_copy(dst, &g_verified_frontier, now);
    pthread_mutex_unlock(&g_state_lock);
    return copied;
}

/**
 * @brief Atomically assigns the next contiguous block to a scan task.
 */
static bool reserve_next_block(scan_task_t *task, uint64_t now) {
    if (!task) return false;
    bool reserved = false;
    pthread_mutex_lock(&g_state_lock);
    if (ttak_bigint_copy(&task->start, &g_next_range_start, now)) {
        reserved = ttak_bigint_add_u64(&g_next_range_start, &g_next_range_start, BLOCK_SIZE, now);
    }
    pthread_mutex_unlock(&g_state_lock);
    if (reserved) {
        task->count = BLOCK_SIZE;
    }
    return reserved;
}

/**
 * @brief Writes a deterministic hash proof to disk without acquiring locks.
 * @note Callers must hold @ref g_log_lock prior to invoking this helper.
 */
static void log_proof_unlocked(const ttak_bigint_t *start, uint64_t count, const char *hash_hex, uint64_t now) {
    if (!start || !hash_hex) return;
    char *start_str = ttak_bigint_to_string(start, now);
    FILE *fp = fopen(g_hash_log_path, "a");
    if (fp) {
        const char *label = start_str ? start_str : "conversion_error";
        fprintf(fp, "{\"range_start\":\"%s\",\"count\":%" PRIu64 ",\"proof_sha256\":\"%s\",\"ts\":%" PRIu64 "}\n",
                label, count, hash_hex, (uint64_t)time(NULL));
        fclose(fp);
    }
    if (start_str) {
        ttak_mem_free(start_str);
    }
}

/**
 * @brief Writes a discovery record for confirmed period-3 sociable seeds.
 */
static void log_found_seed(const ttak_bigint_t *seed, uint64_t now) {
    if (!seed) return;
    char *seed_str = ttak_bigint_to_string(seed, now);
    if (!seed_str) return;
    pthread_mutex_lock(&g_log_lock);
    FILE *fp = fopen(g_found_log_path, "a");
    if (fp) {
        fprintf(fp, "{\"status\":\"found\",\"seed\":\"%s\",\"ts\":%" PRIu64 "}\n", seed_str, (uint64_t)time(NULL));
        fclose(fp);
    }
    pthread_mutex_unlock(&g_log_lock);
    ttak_mem_free(seed_str);
}

/**
 * @brief Advances the verified frontier once a block is fully processed.
 */
static void mark_range_verified(const ttak_bigint_t *start, uint64_t count, uint64_t now) {
    if (!start || count == 0) return;
    ttak_bigint_t candidate;
    ttak_bigint_init(&candidate, now);
    if (!ttak_bigint_copy(&candidate, start, now)) {
        ttak_bigint_free(&candidate, now);
        return;
    }
    if (!ttak_bigint_add_u64(&candidate, &candidate, count, now)) {
        ttak_bigint_free(&candidate, now);
        return;
    }
    pthread_mutex_lock(&g_state_lock);
    if (ttak_bigint_cmp(&candidate, &g_verified_frontier) > 0) {
        ttak_bigint_copy(&g_verified_frontier, &candidate, now);
    }
    pthread_mutex_unlock(&g_state_lock);
    ttak_bigint_free(&candidate, now);
}

/**
 * @brief Encodes a BigInt as decimal ASCII and feeds it into the SHA-256 context.
 */
static void sha256_update_bigint(SHA256_CTX *ctx, const ttak_bigint_t *value, uint64_t now) {
    if (!ctx || !value) return;
    char *repr = ttak_bigint_to_string(value, now);
    if (!repr) return;
    size_t len = strlen(repr);
    if (len > 0) {
        sha256_update(ctx, (const uint8_t *)repr, len);
    }
    ttak_mem_free(repr);
}

/* ========================================================================== */
/*                              Worker Execution                              */
/* ========================================================================== */
/**
 * @brief Worker routine that enforces verify.c's deterministic hashing cadence.
 * @details Each iteration performs two hash updates (seed and s(seed)) before
 *          advancing the seed exactly once, ensuring reproducible transcripts.
 * @param arg Pointer to the allocated scan task payload.
 * @return void* Always NULL after successful retirement.
 */
static void *worker_scan_range(void *arg) {
    scan_task_t *task = (scan_task_t *)arg;
    uint64_t now = monotonic_millis();

    SHA256_CTX sha_ctx;
    memset(&sha_ctx, 0, sizeof(sha_ctx));
    sha256_init(&sha_ctx);

    ttak_bigint_t seed_val, bn_next, bn_s2, bn_s3;
    ttak_bigint_init_u64(&seed_val, 0, now);
    ttak_bigint_copy(&seed_val, &task->start, now);
    ttak_bigint_init(&bn_next, now);
    ttak_bigint_init(&bn_s2, now);
    ttak_bigint_init(&bn_s3, now);

    bool fatal_error = false;
    uint64_t report_step = g_progress_quantum;
    if (report_step == 0 || report_step > task->count) {
        report_step = task->count;
    }
    uint64_t pending_progress = 0;
    uint64_t processed = 0;

    for (uint64_t i = 0; i < task->count; i++) {
        /* Step 1: hash the current seed. */
        sha256_update_bigint(&sha_ctx, &seed_val, now);

        /* Step 2: compute s(n) and hash the result. */
        if (!ttak_sum_proper_divisors_big(&seed_val, &bn_next, now)) {
            fatal_error = true;
            break;
        }
        sha256_update_bigint(&sha_ctx, &bn_next, now);

        /* Step 3: detect period-3 sociable seeds without pruning the loop. */
        if (ttak_bigint_cmp(&bn_next, &seed_val) != 0 && ttak_bigint_cmp_u64(&bn_next, 1) > 0) {
            if (!ttak_sum_proper_divisors_big(&bn_next, &bn_s2, now)) {
                fatal_error = true;
                break;
            }
            if (ttak_bigint_cmp(&bn_s2, &seed_val) != 0) {
                if (!ttak_sum_proper_divisors_big(&bn_s2, &bn_s3, now)) {
                    fatal_error = true;
                    break;
                }
                if (ttak_bigint_cmp(&bn_s3, &seed_val) == 0) {
                    log_found_seed(&seed_val, now);
                }
            }
        }

        /* Step 4: deterministic advancement (seed and progress counters). */
        if (!ttak_bigint_add_u64(&seed_val, &seed_val, 1, now)) {
            fatal_error = true;
            break;
        }

        pending_progress++;
        processed++;
        if (report_step < task->count && pending_progress >= report_step) {
            ttak_atomic_add64(&g_total_scanned, pending_progress);
            pending_progress = 0;
        }
    }

    if (pending_progress > 0) {
        ttak_atomic_add64(&g_total_scanned, pending_progress);
    }

    bool completed_block = !fatal_error && processed == task->count;
    if (completed_block) {
        pthread_mutex_lock(&g_log_lock);
        uint8_t digest[32];
        sha256_final(&sha_ctx, digest);
        char hash_hex[65];
        for (size_t j = 0; j < sizeof(digest); j++) {
            snprintf(hash_hex + (j * 2), 3, "%02x", digest[j]);
        }
        hash_hex[64] = '\0';
        log_proof_unlocked(&task->start, task->count, hash_hex, now);
        pthread_mutex_unlock(&g_log_lock);
        mark_range_verified(&task->start, task->count, now);
    } else if (processed > 0) {
        uint64_t warn_now = monotonic_millis();
        char *range_label = ttak_bigint_to_string(&task->start, warn_now);
        fprintf(stderr,
                "[WARN] Dropping partial proof for range %s (%" PRIu64 "/%" PRIu64 " seeds processed).\n",
                range_label ? range_label : "conversion_error", processed, task->count);
        if (range_label) {
            ttak_mem_free(range_label);
        }
    }

    ttak_bigint_free(&seed_val, now);
    ttak_bigint_free(&bn_next, now);
    ttak_bigint_free(&bn_s2, now);
    ttak_bigint_free(&bn_s3, now);
    ttak_bigint_free(&task->start, now);
    ttak_mem_free(task);

    if (fatal_error) {
        fprintf(stderr, "[ERROR] Worker aborted due to arithmetic failure.\n");
    }

    return NULL;
}

/* ========================================================================== */
/*                             Retirement Watchdog                            */
/* ========================================================================== */
static void *shutdown_watchdog(void *arg) {
    (void)arg;
    struct timespec ts = { .tv_sec = SHUTDOWN_TIMEOUT_S, .tv_nsec = 0 };
    nanosleep(&ts, NULL);
    fprintf(stderr, "\n[FATAL] Shutdown synchronization timed out. Forcing exit.\n");
    _exit(EXIT_FAILURE);
}

/* ========================================================================== */
/*                                 Main Entry                                 */
/* ========================================================================== */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    uint64_t init_now = monotonic_millis();
    ttak_bigint_init_u64(&g_next_range_start, DEFAULT_START_SEED, init_now);
    ttak_bigint_init_u64(&g_verified_frontier, DEFAULT_START_SEED, init_now);
    ensure_log_directory();
    load_checkpoint(init_now);
    configure_progress_quantum();

    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus < 1) cpus = 1;

    ttak_thread_pool_t *pool = ttak_thread_pool_create(cpus, 0, monotonic_millis());
    if (!pool) {
        fprintf(stderr, "[FATAL] Unable to create thread pool.\n");
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("[SYSTEM] Aliquot scanner online with %ld worker threads.\n", cpus);
    char *start_str = ttak_bigint_to_string(&g_verified_frontier, monotonic_millis());
    if (start_str) {
        printf("[SYSTEM] Resuming from seed %s\n", start_str);
        ttak_mem_free(start_str);
    }

    uint64_t last_report = monotonic_millis();
    uint64_t last_checkpoint = last_report;
#if !TTAK_GPU_ACCELERATED
    uint64_t last_rate_report = last_report;
    uint64_t last_rate_total = ttak_atomic_read64(&g_total_scanned);
#endif

    while (!ttak_atomic_read64(&g_shutdown_requested)) {
        scan_task_t *task = ttak_mem_alloc(sizeof(scan_task_t), __TTAK_UNSAFE_MEM_FOREVER__, monotonic_millis());
        if (!task) {
            fprintf(stderr, "[FATAL] Memory allocation failure.\n");
            break;
        }
        uint64_t alloc_now = monotonic_millis();
        ttak_bigint_init(&task->start, alloc_now);
        if (!reserve_next_block(task, alloc_now)) {
            ttak_bigint_free(&task->start, alloc_now);
            ttak_mem_free(task);
            fprintf(stderr, "[FATAL] Failed to reserve next scanning block.\n");
            break;
        }

        if (!ttak_thread_pool_submit_task(pool, worker_scan_range, task, 0, monotonic_millis())) {
            /* Fallback to synchronous execution when the pool is saturated. */
            worker_scan_range(task);
            struct timespec ts = {0, 10000000};
            nanosleep(&ts, NULL);
        }

        uint64_t now = monotonic_millis();
        if (now - last_report >= 5000) {
            ttak_bigint_t next_head;
            ttak_bigint_init(&next_head, now);
            snapshot_next_range_start(&next_head, now);

            ttak_bigint_t verified_head;
            ttak_bigint_init(&verified_head, now);
            snapshot_verified_frontier(&verified_head, now);

            char *next_str = ttak_bigint_to_string(&next_head, now);
            char *verified_str = ttak_bigint_to_string(&verified_head, now);
#if !TTAK_GPU_ACCELERATED
            double instant_rate = 0.0;
            uint64_t current_total = ttak_atomic_read64(&g_total_scanned);
            double interval = (now - last_rate_report) / 1000.0;
            if (interval > 0.0 && current_total >= last_rate_total) {
                instant_rate = (double)(current_total - last_rate_total) / interval;
            }
#endif
            if (next_str && verified_str) {
#if TTAK_GPU_ACCELERATED
                printf("[STATUS] Next %s | Verified %s | Mode GPU_ACCELERATED\n", next_str, verified_str);
#else
                printf("[STATUS] Next %s | Verified %s | Rate %.2f seeds/sec\n", next_str, verified_str, instant_rate);
#endif
            } else if (next_str) {
                printf("[STATUS] Next %s\n", next_str);
            }
            if (next_str) ttak_mem_free(next_str);
            if (verified_str) ttak_mem_free(verified_str);

            ttak_bigint_free(&next_head, now);
            ttak_bigint_free(&verified_head, now);
            last_report = now;
#if !TTAK_GPU_ACCELERATED
            last_rate_report = now;
            last_rate_total = current_total;
#endif
        }

        if (now - last_checkpoint >= CHECKPOINT_INTERVAL) {
            ttak_bigint_t checkpoint_val;
            ttak_bigint_init(&checkpoint_val, now);
            snapshot_verified_frontier(&checkpoint_val, now);
            save_checkpoint(&checkpoint_val, now);
            ttak_bigint_free(&checkpoint_val, now);
            last_checkpoint = now;
        }

        struct timespec ts = {0, 100000};
        nanosleep(&ts, NULL);
    }

    printf("\n[RETIRE] Shutdown requested. Flushing task queue...\n");

    pthread_t watchdog_tid;
    if (pthread_create(&watchdog_tid, NULL, shutdown_watchdog, NULL) == 0) {
        pthread_detach(watchdog_tid);
    }

    ttak_thread_pool_destroy(pool);

    sanitize_logs();

    uint64_t retire_now = monotonic_millis();
    ttak_bigint_t final_snapshot;
    ttak_bigint_init(&final_snapshot, retire_now);
    snapshot_verified_frontier(&final_snapshot, retire_now);
    save_checkpoint(&final_snapshot, retire_now);

    char *final_str = ttak_bigint_to_string(&final_snapshot, retire_now);
    if (final_str) {
        printf("[RETIRE] Final checkpoint: %s\n", final_str);
        ttak_mem_free(final_str);
    }

    ttak_bigint_free(&final_snapshot, retire_now);
    ttak_bigint_free(&g_next_range_start, retire_now);
    ttak_bigint_free(&g_verified_frontier, retire_now);

    printf("[RETIRE] Scanner shutdown complete.\n");
    return EXIT_SUCCESS;
}
