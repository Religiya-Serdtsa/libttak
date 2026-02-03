#include <ttak/limit/limit.h>
#include <ttak/stats/stats.h>
#include <ttak/log/logger.h>
#include <ttak/container/ringbuf.h>
#include <ttak/container/pool.h>
#include <ttak/mem/epoch_gc.h>
#include <ttak/sync/spinlock.h>
#include <ttak/timing/deadline.h>
#include <ttak/mem/mem.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../tests/test_macros.h"

// --- Logger Mock ---
static char last_log_msg[256];
static void mock_log_func(ttak_log_level_t level, const char *msg) {
    snprintf(last_log_msg, sizeof(last_log_msg), "%s", msg);
}

void test_logger() {
    ttak_logger_t logger;
    ttak_logger_init(&logger, mock_log_func, TTAK_LOG_WARN);
    
    last_log_msg[0] = '\0';
    ttak_logger_log(&logger, TTAK_LOG_INFO, "Should not appear");
    ASSERT(strlen(last_log_msg) == 0); 
    
    ttak_logger_log(&logger, TTAK_LOG_ERROR, "Critical Error %d", 404);
    ASSERT(strcmp(last_log_msg, "Critical Error 404") == 0);
}

void test_limit() {
    ttak_ratelimit_t rl;
    // 10 tokens/sec, burst 2
    ttak_ratelimit_init(&rl, 10.0, 2.0); 
    
    ASSERT(ttak_ratelimit_allow(&rl) == true);
    ASSERT(ttak_ratelimit_allow(&rl) == true);
    ASSERT(ttak_ratelimit_allow(&rl) == false); // Burst exceeded
    
    // Sleep 110ms to refill 1 token (10/sec = 1 per 100ms)
    usleep(110000);
    ASSERT(ttak_ratelimit_allow(&rl) == true);
}

void test_stats() {
    ttak_stats_t st;
    ttak_stats_init(&st, 0, 100);
    
    ttak_stats_record(&st, 10);
    ttak_stats_record(&st, 20);
    ttak_stats_record(&st, 30);
    
    ASSERT(st.count == 3);
    ASSERT(st.min == 10);
    ASSERT(st.max == 30);
    ASSERT(ttak_stats_mean(&st) == 20.0);
    
    ttak_stats_print_ascii(&st);
}

void test_ringbuf() {
    ttak_ringbuf_t *rb = ttak_ringbuf_create(5, sizeof(int));
    int in = 10, out = 0;
    
    ASSERT(ttak_ringbuf_is_empty(rb) == true);
    ttak_ringbuf_push(rb, &in);
    ASSERT(ttak_ringbuf_count(rb) == 1);
    ttak_ringbuf_pop(rb, &out);
    ASSERT(out == 10);
    ASSERT(ttak_ringbuf_is_empty(rb) == true);
    
    for (int i=0; i<5; i++) ttak_ringbuf_push(rb, &i);
    ASSERT(ttak_ringbuf_is_full(rb) == true);
    ASSERT(ttak_ringbuf_push(rb, &in) == false); // Overflow
    
    ttak_ringbuf_destroy(rb);
}

void test_pool() {
    ttak_object_pool_t *pool = ttak_object_pool_create(10, 64);
    void *p1 = ttak_object_pool_alloc(pool);
    void *p2 = ttak_object_pool_alloc(pool);
    
    ASSERT(p1 != NULL);
    ASSERT(p2 != NULL);
    ASSERT(p1 != p2);
    
    ttak_object_pool_free(pool, p1);
    void *p3 = ttak_object_pool_alloc(pool);
    ASSERT(p3 == p1); // Should reuse simple bitmap logic (lowest bit)
    
    ttak_object_pool_destroy(pool);
}

void test_epoch_gc() {
    ttak_epoch_gc_t gc;
    ttak_epoch_gc_init(&gc);
    
    void *ptr = ttak_mem_alloc(100, __TTAK_UNSAFE_MEM_FOREVER__, 0);
    ttak_epoch_gc_register(&gc, ptr, 100);
    
    // Simulate usage
    gc.current_epoch = 1;
    ttak_epoch_gc_rotate(&gc); // Epoch -> 2
    
    // We cannot easily verify "freed" without hooking free, but we ensure no crash.
    ttak_epoch_gc_destroy(&gc);
}

void test_sync_timing() {
    ttak_spin_t lock;
    ttak_spin_init(&lock);
    ttak_spin_lock(&lock);
    ASSERT(ttak_spin_trylock(&lock) == false);
    ttak_spin_unlock(&lock);
    ASSERT(ttak_spin_trylock(&lock) == true);
    ttak_spin_unlock(&lock);
    
    ttak_deadline_t dl;
    ttak_deadline_set(&dl, 100);
    ASSERT(ttak_deadline_is_expired(&dl) == false);
    usleep(110000);
    ASSERT(ttak_deadline_is_expired(&dl) == true);
}

int main() {
    printf("=== Test: New Features ===\n");
    RUN_TEST(test_logger);
    RUN_TEST(test_limit);
    RUN_TEST(test_stats);
    RUN_TEST(test_ringbuf);
    RUN_TEST(test_pool);
    RUN_TEST(test_epoch_gc);
    RUN_TEST(test_sync_timing);
    printf("=== All New Feature Tests Passed ===\n");
    return 0;
}