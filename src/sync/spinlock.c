#include <ttak/sync/spinlock.h>
#include <ttak/types/ttak_compiler.h>
#include <time.h>
#include <stdlib.h>
#include <sched.h>
#ifdef _WIN32
#  include <windows.h>
#endif

static inline void ttak_spin_relax(void) {
#ifdef _WIN32
    YieldProcessor();
#elif defined(__TINYC__) && TTAK_TINYCC_NEEDS_PORTABLE_FALLBACK
    sched_yield();
#elif defined(__x86_64__) || defined(__i386__)
#if defined(__TINYC__)
    __asm__ __volatile__("pause");
#else
    __builtin_ia32_pause();
#endif
#elif defined(__aarch64__) && !defined(__TINYC__)
    __asm__ __volatile__("yield");
#else
    (void)0;
#endif
}

/**
 * @brief Performs adaptive backoff.
 */
void ttak_backoff_pause(ttak_backoff_t *b) {
    int local_count = b->count;
    int local_limit = b->limit;
    for (int i = 0; i < local_count; i++) {
        ttak_spin_relax();
    }
    if (local_count < local_limit) {
        b->count = local_count + 1;
    } else {
#ifdef _WIN32
        Sleep(0);
#else
        sched_yield();
#endif
    }
}

/**
 * @brief Spins with backoff until acquired.
 */
void ttak_spin_lock(ttak_spin_t *lock) {
#if defined(__TINYC__) && defined(__x86_64__)
    /* Ultra-fast path for TCC using inline assembly */
    int loop = 0;
    while (1) {
        unsigned char old = 1;
        __asm__ volatile (
            "lock; xchg %0, %1"
            : "+r" (old), "+m" (lock->flag)
            : : "memory"
        );
        if (!old) return;
        
        /* Backoff */
        if (++loop < 100) {
            __asm__ volatile ("pause");
        } else {
            sched_yield();
            loop = 0;
        }
    }
#else
    ttak_backoff_t bo;
    ttak_backoff_init(&bo);
    while (atomic_flag_test_and_set_explicit((atomic_flag *)&lock->flag, memory_order_acquire)) {
        ttak_backoff_pause(&bo);
    }
#endif
}
