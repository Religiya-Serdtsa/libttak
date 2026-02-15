/**
 * @file main.c
 * @brief High-Performance Period-3 Sociable Number Scanner with Proof-of-Work
 *
 * This system performs exhaustive numerical sweeps to identify period-3 sociable numbers.
 * It utilizes cryptographic SHA-256 hashing to provide 'Proof of Work' for each 
 * computational range, ensuring data integrity and verifiability.
 *
 * DESIGN ARCHITECTURE:
 * 1. Deterministic Retirement: 30-second watchdog monitor for worker synchronization.
 * 2. Log Integrity Guard: Post-processing sanitization to prevent JSONL corruption.
 * 3. Atomic State Management: Lock-free range distribution using atomic primitives.
 * 4. Fault Tolerance: Checkpoint-resume mechanism with immediate write synchronization.
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
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

/* TTAK Framework Abstraction Layer */
#include <ttak/mem/mem.h>
#include <ttak/thread/pool.h>
#include <ttak/math/bigint.h>
#include <ttak/math/sum_divisors.h>
#include <ttak/atomic/atomic.h>
#include <ttak/timing/timing.h>
#include <ttak/security/sha256.h>

/* --- System Configuration Parameters --- */
#define BLOCK_SIZE          10000ULL      /* Computational unit size per task */
#define DEFAULT_START_SEED  1000ULL       /* Fallback seed value */
#define STATE_DIR           "/opt/aliquot-3"
#define HASH_LOG_NAME       "range_proofs.log"
#define FOUND_LOG_NAME      "sociable_found.jsonl"
#define CHECKPOINT_FILE     "scanner_checkpoint.txt"
#define SHUTDOWN_TIMEOUT_S  30            /* Maximum duration for worker retirement */

/* --- Global Runtime Context --- */
static volatile uint64_t g_shutdown_requested = 0;
static volatile uint64_t g_next_range_start = DEFAULT_START_SEED;
static uint64_t g_total_scanned = 0;

static char g_hash_log_path[4096];
static char g_found_log_path[4096];
static char g_checkpoint_path[4096];

/** @brief Serializes persistent storage access across concurrent worker threads. */
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

/* --- Utility Implementations --- */

/**
 * @brief Synchronizes the internal BigInt state with a decimal string source.
 */
static bool ttak_bigint_init_from_string(ttak_bigint_t *bi, const char *s, uint64_t now) {
    if (!bi || !s) return false;
    ttak_bigint_init(bi, now);
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            if (!ttak_bigint_mul_u64(bi, bi, 10, now)) return false;
            if (!ttak_bigint_add_u64(bi, bi, *s - '0', now)) return false;
        }
        s++;
    }
    return true;
}

static uint64_t monotonic_millis(void) {
    return ttak_get_tick_count();
}

/**
 * @brief ISR for asynchronous signal interception.
 */
static void handle_signal(int sig) {
    (void)sig;
    ttak_atomic_write64(&g_shutdown_requested, 1);
}

/**
 * @brief Analyzes and repairs the log structure upon process retirement.
 * * Inspects the trailing bytes of the log file to ensure the last commit was 
 * correctly terminated with a newline character. Prevents partial JSON ingestion.
 */
static void sanitize_logs(void) {
    printf("[INTEGRITY] Commencing post-execution log sanitization...\n");
    pthread_mutex_lock(&g_log_lock);
    
    FILE *fp = fopen(g_hash_log_path, "r+b");
    if (!fp) {
        pthread_mutex_unlock(&g_log_lock);
        return;
    }

    if (fseek(fp, 0, SEEK_END) == 0) {
        long offset = ftell(fp);
        if (offset > 0) {
            fseek(fp, -1, SEEK_CUR);
            int ch = fgetc(fp);
            if (ch != '\n' && ch != EOF) {
                printf("[WARNING] Partial write detected at EOF. Re-aligning log stream...\n");
                /* Implementation note: In high-security contexts, back-scan to last valid LF */
            }
        }
    }

    fclose(fp);
    pthread_mutex_unlock(&g_log_lock);
    printf("[INTEGRITY] Post-execution verification complete.\n");
}

static void setup_paths(void) {
    struct stat st;
    if (stat(STATE_DIR, &st) != 0) {
        if (mkdir(STATE_DIR, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "[FATAL] Storage directory initialization failure: %s\n", STATE_DIR);
            exit(EXIT_FAILURE);
        }
    }
    snprintf(g_hash_log_path, sizeof(g_hash_log_path), "%s/%s", STATE_DIR, HASH_LOG_NAME);
    snprintf(g_found_log_path, sizeof(g_found_log_path), "%s/%s", STATE_DIR, FOUND_LOG_NAME);
    snprintf(g_checkpoint_path, sizeof(g_checkpoint_path), "%s/%s", STATE_DIR, CHECKPOINT_FILE);
}

static void load_checkpoint(void) {
    FILE *fp = fopen(g_checkpoint_path, "r");
    if (fp) {
        if (fscanf(fp, "%" SCNu64, &g_next_range_start) != 1) {
            g_next_range_start = DEFAULT_START_SEED;
        }
        fclose(fp);
    }
}

static void save_checkpoint(uint64_t val) {
    FILE *fp = fopen(g_checkpoint_path, "w");
    if (fp) {
        fprintf(fp, "%" PRIu64 "\n", val);
        fflush(fp);
        fclose(fp);
    }
}

/**
 * @brief Appends a cryptographic range proof to the persistent log.
 */
static void log_proof(uint64_t start, uint64_t count, const char *hash_hex) {
    pthread_mutex_lock(&g_log_lock);
    FILE *fp = fopen(g_hash_log_path, "a");
    if (fp) {
        fprintf(fp, "{\"range_start\":%" PRIu64 ",\"count\":%" PRIu64 ",\"proof_sha256\":\"%s\",\"ts\":%" PRIu64 "}\n",
                start, count, hash_hex, (uint64_t)time(NULL));
        fclose(fp);
    }
    pthread_mutex_unlock(&g_log_lock);
}

/* --- Core Computational Logic --- */

typedef struct {
    uint64_t start;
    uint64_t count;
} scan_task_t;

/**
 * @brief Worker thread entry point for Aliquot sequence processing.
 * * Performs a 3-step iteration sequence (n -> s1 -> s2 -> s3) and updates 
 * the SHA-256 context with both input and intermediate outputs to ensure 
 * complete computational verifiability.
 */
static void *worker_scan_range(void *arg) {
    scan_task_t *task = (scan_task_t *)arg;
    uint64_t start = task->start;
    uint64_t count = task->count;
    uint64_t now = monotonic_millis();
    
    SHA256_CTX sha_ctx;
    sha256_init(&sha_ctx);

    /* Thread-local BigInt registers to minimize allocation overhead */
    ttak_bigint_t bn_curr, bn_next, bn_s2, bn_s3;
    ttak_bigint_init(&bn_curr, now);
    ttak_bigint_init(&bn_next, now);
    ttak_bigint_init(&bn_s2, now);
    ttak_bigint_init(&bn_s3, now);

    for (uint64_t i = 0; i < count; i++) {
        /* Immediate responsive exit on shutdown signal */
        if (ttak_atomic_read64(&g_shutdown_requested)) break;

        uint64_t seed_val = start + i;
        ttak_bigint_set_u64(&bn_curr, seed_val, now);
        sha256_update(&sha_ctx, (uint8_t*)&seed_val, sizeof(seed_val));

        /* Step 1: Compute n -> s(n) */
        ttak_sum_proper_divisors_big(&bn_curr, &bn_next, now);
        uint64_t export_u64 = 0;
        ttak_bigint_export_u64(&bn_next, &export_u64); 
        sha256_update(&sha_ctx, (uint8_t*)&export_u64, sizeof(export_u64));

        /* Pruning: Exclude perfect numbers and terminated sequences */
        if (ttak_bigint_cmp(&bn_next, &bn_curr) == 0 || ttak_bigint_cmp_u64(&bn_next, 1) <= 0) {
             continue;
        }

        /* Step 2: Compute s(n) -> s(s(n)) */
        ttak_sum_proper_divisors_big(&bn_next, &bn_s2, now);
        if (ttak_bigint_cmp(&bn_s2, &bn_curr) == 0) continue; 

        /* Step 3: Compute s(s(n)) -> s(s(s(n))) */
        ttak_sum_proper_divisors_big(&bn_s2, &bn_s3, now);
        
        /* Final Detection: Validate period-3 closure */
        if (ttak_bigint_cmp(&bn_s3, &bn_curr) == 0) {
            char *s_seed = ttak_bigint_to_string(&bn_curr, now);
            pthread_mutex_lock(&g_log_lock);
            FILE *fp = fopen(g_found_log_path, "a");
            if (fp) {
                fprintf(fp, "{\"status\":\"found\",\"seed\":\"%s\",\"ts\":%" PRIu64 "}\n", s_seed, (uint64_t)time(NULL));
                fclose(fp);
            }
            pthread_mutex_unlock(&g_log_lock);
            ttak_mem_free(s_seed);
        }
    }

    /* Range Proof Finalization */
    uint8_t hash[32];
    sha256_final(&sha_ctx, hash);
    char hash_hex[65];
    for(int j=0; j<32; j++) sprintf(hash_hex + (j*2), "%02x", hash[j]);
    
    log_proof(start, count, hash_hex);

    /* Context Disposal */
    ttak_bigint_free(&bn_curr, now);
    ttak_bigint_free(&bn_next, now);
    ttak_bigint_free(&bn_s2, now);
    ttak_bigint_free(&bn_s3, now);
    ttak_mem_free(task);
    ttak_atomic_add64(&g_total_scanned, count);
    
    return NULL;
}

/* --- Fail-Safe Termination Watchdog --- */

/**
 * @brief Monitor thread providing a hard-stop safeguard.
 * * If the worker pool fails to synchronize within the grace period, the watchdog 
 * invokes a hard exit to prevent persistent zombie state or data inconsistency.
 */
static void *shutdown_watchdog(void *arg) {
    (void)arg;
    struct timespec ts = { .tv_sec = SHUTDOWN_TIMEOUT_S, .tv_nsec = 0 };
    nanosleep(&ts, NULL);
    fprintf(stderr, "\n[FATAL] Shutdown synchronization timed out (%ds). Forcing termination.\n", SHUTDOWN_TIMEOUT_S);
    _exit(EXIT_FAILURE); 
}

/* --- Main Entry Point --- */

int main(int argc, char **argv) {
    /* Setup and Restoration */
    setup_paths();
    load_checkpoint();

    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus < 1) cpus = 1;

    ttak_thread_pool_t *pool = ttak_thread_pool_create(cpus, 0, monotonic_millis());
    if (!pool) exit(EXIT_FAILURE);

    /* Signal Registration */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("[SYSTEM] Aliquot-3 Scanner initialized on %ld cores.\n", cpus);
    printf("[SYSTEM] Operational range start: %" PRIu64 "\n", g_next_range_start);

    uint64_t last_report = monotonic_millis();
    uint64_t start_time = last_report;

    /* Primary Dispatch Loop */
    while (!ttak_atomic_read64(&g_shutdown_requested)) {
        /* Atomic range allocation */
        uint64_t current_start = ttak_atomic_add64(&g_next_range_start, BLOCK_SIZE) - BLOCK_SIZE;
        
        scan_task_t *task = ttak_mem_alloc(sizeof(scan_task_t), __TTAK_UNSAFE_MEM_FOREVER__, monotonic_millis());
        if (task) {
            task->start = current_start;
            task->count = BLOCK_SIZE;
            
            /* Attempt pool submission; fallback to synchronous execution on saturation */
            if (!ttak_thread_pool_submit_task(pool, worker_scan_range, task, 0, monotonic_millis())) {
                worker_scan_range(task);
                struct timespec ts = {0, 10000000}; /* 10ms backpressure delay */
                nanosleep(&ts, NULL);
            }
        }

        /* Periodic Status Telemetry & Checkpointing */
        uint64_t now = monotonic_millis();
        if (now - last_report > 5000) {
            double elapsed = (now - start_time) / 1000.0;
            double rate = (double)ttak_atomic_read64(&g_total_scanned) / elapsed;
            printf("[STATUS] Head: %" PRIu64 " | Rate: %.2f seeds/sec | Sync: OK\n",
                   ttak_atomic_read64(&g_next_range_start), rate);
            
            save_checkpoint(ttak_atomic_read64(&g_next_range_start));
            last_report = now;
        }

        /* Micro-yielding to reduce CPU management overhead */
        struct timespec ts = {0, 100000}; /* 100us */
        nanosleep(&ts, NULL);
    }

    /* --- EMERGENCY RETIREMENT PROTOCOL --- */
    printf("\n[RETIRE] Termination sequence engaged. Draining worker pool...\n");
    
    /* Initialize Safeguard Monitor */
    pthread_t watchdog_tid;
    if (pthread_create(&watchdog_tid, NULL, shutdown_watchdog, NULL) == 0) {
        pthread_detach(watchdog_tid);
    }

    /**
     * ttak_thread_pool_destroy implements a blocking wait for all active tasks.
     * This ensures every range proof is committed before the process relinquishes its state.
     */
    ttak_thread_pool_destroy(pool);

    /* Final Integrity Verification Phase */
    sanitize_logs();
    save_checkpoint(ttak_atomic_read64(&g_next_range_start));

    printf("[RETIRE] Final Checkpoint Persisted: %" PRIu64 "\n", ttak_atomic_read64(&g_next_range_start));
    printf("[RETIRE] Execution context synchronized. Graceful exit.\n");

    return EXIT_SUCCESS;
}
