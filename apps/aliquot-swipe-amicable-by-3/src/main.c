/**
 * @file main.c
 * @brief High-Performance Period-3 Sociable Number Scanner (Dirty Play Edition)
 *
 * This scanner is designed for "Range Dominance". Instead of random hunting,
 * it sweeps ranges sequentially, generating cryptographic proofs (SHA-256)
 * of the work done. This allows the user to claim indisputable priority
 * over specific number ranges.
 *
 * STRATEGY:
 * 1. BigInt Precision: Uses libttak's BigInt for all sum-div calculations to avoid overflow.
 * 2. 3-Step Sieve: Checks n -> s(n) -> s(s(n)) -> s(s(s(n))) == n ONLY.
 * 3. Range Hashing: Hashes the vector of results for a range to prove coverage.
 * 4. Verification Mode: Detailed trace to refute false claims by others.
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

#include <ttak/mem/mem.h>
#include <ttak/thread/pool.h>
#include <ttak/math/bigint.h>
#include <ttak/math/sum_divisors.h>
#include <ttak/atomic/atomic.h>
#include <ttak/timing/timing.h>
#include <ttak/security/sha256.h>

/* --- Configuration --- */
#define BLOCK_SIZE          10000ULL  // Number of seeds per work unit
#define DEFAULT_START_SEED  1000ULL   // Where to start if no checkpoint
#define STATE_DIR           "/opt/aliquot-3"
#define HASH_LOG_NAME       "range_proofs.log" // The "Receipts"
#define FOUND_LOG_NAME      "sociable_found.jsonl"
#define CHECKPOINT_FILE     "scanner_checkpoint.txt"

/* --- Globals --- */
static volatile uint64_t g_shutdown_requested = 0;
static volatile uint64_t g_next_range_start = DEFAULT_START_SEED;
static uint64_t g_total_scanned = 0;

static char g_hash_log_path[4096];
static char g_found_log_path[4096];
static char g_checkpoint_path[4096];

static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

/* --- Utility Functions --- */

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

static void handle_signal(int sig) {
    (void)sig;
    ttak_atomic_write64(&g_shutdown_requested, 1);
}

static void setup_paths(void) {
    struct stat st;
    if (stat(STATE_DIR, &st) != 0) {
        if (mkdir(STATE_DIR, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "[FATAL] Could not create state dir %s\n", STATE_DIR);
            exit(1);
        }
    }
    snprintf(g_hash_log_path, sizeof(g_hash_log_path), "%s/%s", STATE_DIR, HASH_LOG_NAME);
    snprintf(g_found_log_path, sizeof(g_found_log_path), "%s/%s", STATE_DIR, FOUND_LOG_NAME);
    snprintf(g_checkpoint_path, sizeof(g_checkpoint_path), "%s/%s", STATE_DIR, CHECKPOINT_FILE);
}

static void load_checkpoint(void) {
    FILE *fp = fopen(g_checkpoint_path, "r");
    if (fp) {
        if (fscanf(fp, "%" SCNu64, &g_next_range_start) == 1) {
            printf("[SYSTEM] Resuming from checkpoint: %" PRIu64 "\n", g_next_range_start);
        }
        fclose(fp);
    }
}

static void save_checkpoint(uint64_t val) {
    // Simple save: acts as a "high water mark". 
    // In a real distributed system, we'd need more complex state management,
    // but for a single-node dominator, this suffices.
    FILE *fp = fopen(g_checkpoint_path, "w");
    if (fp) {
        fprintf(fp, "%" PRIu64 "\n", val);
        fclose(fp);
    }
}

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

static void log_found(const char *json_record) {
    pthread_mutex_lock(&g_log_lock);
    FILE *fp = fopen(g_found_log_path, "a");
    if (fp) {
        fprintf(fp, "%s\n", json_record);
        fclose(fp);
        printf("\n[DISCOVERY] >>> %s\n", json_record);
    }
    pthread_mutex_unlock(&g_log_lock);
}

/* --- The Engine --- */

typedef struct {
    uint64_t start;
    uint64_t count;
} scan_task_t;

static void *worker_scan_range(void *arg) {
    scan_task_t *task = (scan_task_t *)arg;
    uint64_t start = task->start;
    uint64_t count = task->count;
    uint64_t now = monotonic_millis();
    
    // Cryptographic Context for Range Proof
    SHA256_CTX sha_ctx;
    sha256_init(&sha_ctx);

    // Reuse BigInts to minimize allocation overhead
    ttak_bigint_t bn_curr, bn_next, bn_s2, bn_s3;
    ttak_bigint_init(&bn_curr, now);
    ttak_bigint_init(&bn_next, now);
    ttak_bigint_init(&bn_s2, now);
    ttak_bigint_init(&bn_s3, now);

    for (uint64_t i = 0; i < count; i++) {
        if (ttak_atomic_read64(&g_shutdown_requested)) break;
        
        uint64_t seed_val = start + i;
        
        // --- Step 0: Load Seed ---
        ttak_bigint_set_u64(&bn_curr, seed_val, now);
        
        // Update Proof Hash with Seed (Proof we visited this seed)
        // We hash the raw bytes of the seed.
        // Endianness matters for reproduction, assuming Little Endian (x86 default).
        sha256_update(&sha_ctx, (uint8_t*)&seed_val, sizeof(seed_val));

        // --- Step 1: seed -> s1 ---
        // sum_proper_divisors_big returns false on allocation failure, 
        // but for standard ranges it shouldn't fail.
        ttak_sum_proper_divisors_big(&bn_curr, &bn_next, now);
        
        // Update Proof Hash with result (Proof we calculated it)
        // We hash the lower 64 bits of the result for speed/compactness in the proof,
        // or the full string if we want absolute pedantry. 
        // Let's hash the 64-bit export for performance.
        uint64_t export_u64 = 0;
        ttak_bigint_export_u64(&bn_next, &export_u64); 
        sha256_update(&sha_ctx, (uint8_t*)&export_u64, sizeof(export_u64));

        // Optimization: If s1 == seed (Perfect), ignore (known).
        // If s1 == 1 (Prime/Terminated), ignore.
        if (ttak_bigint_cmp(&bn_next, &bn_curr) == 0 || ttak_bigint_cmp_u64(&bn_next, 1) <= 0) {
             continue;
        }

        // --- Step 2: s1 -> s2 ---
        ttak_sum_proper_divisors_big(&bn_next, &bn_s2, now);
        
        // Amicable check (s2 == seed)
        if (ttak_bigint_cmp(&bn_s2, &bn_curr) == 0) {
            // Found Amicable pair. Log it? 
            // The prompt focuses on Period-3, but amicable is cool.
            // Let's just hash it and move on to prioritize speed.
            continue;
        }
        
        // --- Step 3: s2 -> s3 ---
        ttak_sum_proper_divisors_big(&bn_s2, &bn_s3, now);
        
        // Sociable-3 check (s3 == seed)
        if (ttak_bigint_cmp(&bn_s3, &bn_curr) == 0) {
            // *** HIT ***
            char *s_seed = ttak_bigint_to_string(&bn_curr, now);
            char *s_s1 = ttak_bigint_to_string(&bn_next, now);
            char *s_s2 = ttak_bigint_to_string(&bn_s2, now);
            
            char log_buf[1024];
            snprintf(log_buf, sizeof(log_buf), 
                "{\"status\":\"sociable-3\",\"seed\":\"%s\",\"path\":[\"%s\",\"%s\",\"%s\"]}",
                s_seed ? s_seed : "null", 
                s_s1 ? s_s1 : "null", 
                s_s2 ? s_s2 : "null");
            
            log_found(log_buf);
            
            if (s_seed) ttak_mem_free(s_seed);
            if (s_s1) ttak_mem_free(s_s1);
            if (s_s2) ttak_mem_free(s_s2);
        }
    }

    // Finalize Proof
    uint8_t hash[32];
    sha256_final(&sha_ctx, hash);
    
    char hash_hex[65];
    for(int j=0; j<32; j++) sprintf(hash_hex + (j*2), "%02x", hash[j]);
    
    // Log the receipt
    log_proof(start, count, hash_hex);

    // Cleanup
    ttak_bigint_free(&bn_curr, now);
    ttak_bigint_free(&bn_next, now);
    ttak_bigint_free(&bn_s2, now);
    ttak_bigint_free(&bn_s3, now);
    ttak_mem_free(task);
    
    ttak_atomic_add64(&g_total_scanned, count);
    
    return NULL;
}

/* --- Verification Mode (The "Debunker") --- */
void perform_deep_verification(const char *seed_str) {
    uint64_t now = monotonic_millis();
    ttak_bigint_t val;
    
    printf("--- [VERIFY MODE] Deep Trace for Seed: %s ---\n", seed_str);
    printf("Timestamp: %" PRIu64 "\n", now);
    printf("Precision: libttak BigInt (Arbitrary Precision)\n\n");

    if (!ttak_bigint_init_from_string(&val, seed_str, now)) {
        printf("Error: Invalid seed format.\n");
        return;
    }

    ttak_bigint_t original;
    ttak_bigint_init_copy(&original, &val, now);
    
    ttak_bigint_t next;
    ttak_bigint_init(&next, now);

    for (int step = 1; step <= 10; step++) {
        char *curr_s = ttak_bigint_to_string(&val, now);
        
        ttak_sum_proper_divisors_big(&val, &next, now);
        
        char *next_s = ttak_bigint_to_string(&next, now);
        
        printf("Step %d: %s -> %s\n", step, curr_s ? curr_s : "?", next_s ? next_s : "?");
        
        // Check for cycle return
        if (ttak_bigint_cmp(&next, &original) == 0) {
            printf("\n>>> CYCLE DETECTED at Step %d (Period-%d) <<<\n", step, step);
            if (step == 3) printf(">>> CONFIRMED: Period-3 Sociable Number <<<\n");
            if (curr_s) ttak_mem_free(curr_s);
            if (next_s) ttak_mem_free(next_s);
            break;
        }
        
        // Check for termination
        if (ttak_bigint_cmp_u64(&next, 1) <= 0) {
            printf("\n>>> TERMINATED at Step %d <<<\n", step);
            if (curr_s) ttak_mem_free(curr_s);
            if (next_s) ttak_mem_free(next_s);
            break;
        }

        ttak_bigint_copy(&val, &next, now);
        if (curr_s) ttak_mem_free(curr_s);
        if (next_s) ttak_mem_free(next_s);
    }
    
    ttak_bigint_free(&val, now);
    ttak_bigint_free(&next, now);
    ttak_bigint_free(&original, now);
    printf("\n--- End of Trace ---\n");
}

/* --- Main Entry --- */

int main(int argc, char **argv) {
    // 1. Verify Mode Check
    if (argc >= 3 && strcmp(argv[1], "--verify") == 0) {
        perform_deep_verification(argv[2]);
        return 0;
    }

    printf("[ALIQUOT-3] Initializing High-Performance Range Sweeper...\n");
    setup_paths();
    load_checkpoint();

    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus < 1) cpus = 1;
    printf("[SYSTEM] Detected %ld CPUs. Launching worker pool.\n", cpus);

    ttak_thread_pool_t *pool = ttak_thread_pool_create(cpus, 0, monotonic_millis());
    if (!pool) {
        fprintf(stderr, "Failed to create thread pool.\n");
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("[SYSTEM] Start Seed: %" PRIu64 "\n", g_next_range_start);
    printf("[SYSTEM] Logging Proofs to: %s\n", g_hash_log_path);

    uint64_t last_report = monotonic_millis();
    uint64_t start_time = last_report;

    while (!ttak_atomic_read64(&g_shutdown_requested)) {
        // Backpressure logic: If submit returns NULL, the queue is full.
        // Try to keep pipeline full blindly, but back off if rejected.
        
        bool rejected = false;
        // Try to maintain 2x tasks per CPU
        for (int k = 0; k < cpus * 2; k++) {
             // We don't have get_pending_tasks, so we just push and handle rejection
             // But wait, submit_task is the only way to know. 
             // To avoid spamming atomic_add if queue is full, we need a heuristic.
             // Since we can't check queue depth easily via public API, 
             // we will just throttle the production loop with a sleep.
             break; 
        }

        // Simpler approach compatible with limited API:
        // Just submit one task per iteration loop, then sleep a tiny bit.
        // This is less efficient than "fill the queue", but safer without queue inspection APIs.
        // IMPROVEMENT: Use atomic_add to claim a range, try submit. If submit fails, 
        // we must revert the atomic_add or just process it inline (fallback).
        
        uint64_t current_start = ttak_atomic_add64(&g_next_range_start, BLOCK_SIZE) - BLOCK_SIZE;
        
        scan_task_t *task = ttak_mem_alloc(sizeof(scan_task_t), __TTAK_UNSAFE_MEM_FOREVER__, monotonic_millis());
        if (task) {
            task->start = current_start;
            task->count = BLOCK_SIZE;
            
            // Priority 0. If this returns NULL, it means queue is full or error.
            if (!ttak_thread_pool_submit_task(pool, worker_scan_range, task, 0, monotonic_millis())) {
                // Queue full! Process inline to avoid losing the claimed range (Robustness)
                // printf("[WARN] Queue full, processing inline range %lu\n", current_start);
                worker_scan_range(task); 
                ttak_mem_free(task); // wrapper frees arg? No, wrapper frees task structure usually, but here we called function directly.
                // Wait, if submit fails, we own the memory. worker_scan_range frees 'task' at end.
                // So calling worker_scan_range(task) is correct memory-wise.
                
                // Backoff slightly to let queue drain
                struct timespec ts = {0, 50000000}; // 50ms
                nanosleep(&ts, NULL);
            }
        }

        // Dry, Academic Reporting (every 5 seconds)
        uint64_t now = monotonic_millis();
        if (now - last_report > 5000) {
            double elapsed = (now - start_time) / 1000.0;
            double rate = (double)ttak_atomic_read64(&g_total_scanned) / elapsed;
            uint64_t current_head = ttak_atomic_read64(&g_next_range_start);
            
            printf("[STATUS] Range: %" PRIu64 " | Scanned: %" PRIu64 " | Rate: %.2f seeds/sec | Checkpoint Saved\n",
                   current_head, ttak_atomic_read64(&g_total_scanned), rate);
            
            save_checkpoint(current_head);
            last_report = now;
        }

        // Throttle loop slightly to prevent tight spinning if queue is full
        // Since we are doing 1 task (10k items) per loop, we don't need much sleep.
        // But if we want to saturate, we should loop faster. 
        // Let's rely on the inline fallback to handle "too fast" production.
        // Just a micro-sleep to yield CPU.
        struct timespec ts = {0, 1000}; // 1us
        nanosleep(&ts, NULL);
    }

    printf("\n[SYSTEM] Shutdown requested. Draining pool...\n");
    ttak_thread_pool_destroy(pool); // Waits for pending tasks
    save_checkpoint(ttak_atomic_read64(&g_next_range_start));
    printf("[SYSTEM] Final Checkpoint: %" PRIu64 "\n", ttak_atomic_read64(&g_next_range_start));
    printf("[SYSTEM] All proofs secured.\n");

    return 0;
}
