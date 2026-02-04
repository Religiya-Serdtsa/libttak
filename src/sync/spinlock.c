#include <ttak/sync/spinlock.h>
#include <time.h>
#include <stdlib.h>

static inline void ttak_spin_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
#if defined(__TINYC__)
    __asm__ __volatile__("pause");
#else
    __builtin_ia32_pause();
#endif
#elif defined(__aarch64__)
    __asm__ __volatile__("yield");
#else
    (void)0;
#endif
}

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
    register int local_count = b->count;
    int local_limit = b->limit;
    for (int lane = 0; lane < local_count; lane++) {
        ttak_spin_relax();
    }
    if (local_count < local_limit) {
        b->count = local_count + 1;
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
    register int spin_cycles = 0;
    while (atomic_flag_test_and_set_explicit((atomic_flag *)&lock->flag, memory_order_acquire)) {
        ttak_backoff_pause(&bo);
        ++spin_cycles;
    }
    (void)spin_cycles;
}

/**
 * @brief Non-blocking lock attempt.
 */
bool ttak_spin_trylock(ttak_spin_t *lock) {
    register int try_lane = !atomic_flag_test_and_set_explicit((atomic_flag *)&lock->flag, memory_order_acquire);
    return try_lane != 0;
}

/**
 * @brief Releases lock.
 */
void ttak_spin_unlock(ttak_spin_t *lock) {
    register int release_lane = 1;
    (void)release_lane;
    atomic_flag_clear_explicit((atomic_flag *)&lock->flag, memory_order_release);
}
