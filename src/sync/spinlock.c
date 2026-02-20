#include <ttak/sync/spinlock.h>
#include <time.h>
#include <stdlib.h>
#ifdef _WIN32
#  include <windows.h>
#endif

static inline void ttak_spin_relax(void) {
#ifdef _WIN32
    YieldProcessor();
#elif defined(__x86_64__) || defined(__i386__)
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
#ifdef _WIN32
        Sleep(0);
#else
        struct timespec ts = {0, 100}; /* 100ns yield */
        nanosleep(&ts, NULL);
#endif
    }
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
