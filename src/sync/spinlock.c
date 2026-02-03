#include <ttak/sync/spinlock.h>
#include <time.h>
#include <stdlib.h>

/**
 * @brief Initializes backoff counters.
 */
void ttak_backoff_init(ttak_backoff_t *b) {
    b->count = 0;
    b->limit = 10;
}

/**
 * @brief Performs adaptive backoff.
 */
void ttak_backoff_pause(ttak_backoff_t *b) {
    for (int i = 0; i < b->count; i++) {
        __builtin_ia32_pause(); // x86 specific hint to CPU pipeline
    }
    if (b->count < b->limit) {
        b->count++;
    } else {
        struct timespec ts = {0, 100}; // 100ns yield
        nanosleep(&ts, NULL);
    }
}

/**
 * @brief Clears the lock flag.
 */
void ttak_spin_init(ttak_spin_t *lock) {
    atomic_flag_clear(&lock->flag);
}

/**
 * @brief Spins with backoff until acquired.
 */
void ttak_spin_lock(ttak_spin_t *lock) {
    ttak_backoff_t bo;
    ttak_backoff_init(&bo);
    while (atomic_flag_test_and_set_explicit(&lock->flag, memory_order_acquire)) {
        ttak_backoff_pause(&bo);
    }
}

/**
 * @brief Non-blocking lock attempt.
 */
bool ttak_spin_trylock(ttak_spin_t *lock) {
    return !atomic_flag_test_and_set_explicit(&lock->flag, memory_order_acquire);
}

/**
 * @brief Releases lock.
 */
void ttak_spin_unlock(ttak_spin_t *lock) {
    atomic_flag_clear_explicit(&lock->flag, memory_order_release);
}