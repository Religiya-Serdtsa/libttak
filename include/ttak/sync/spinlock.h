#ifndef TTAK_SYNC_SPINLOCK_H
#define TTAK_SYNC_SPINLOCK_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/**
 * @brief Backoff strategy structure for spinlocks.
 * 
 * Used to reduce contention by pausing or yielding during busy-wait loops.
 */
typedef struct ttak_backoff {
    volatile int count; /**< Current backoff iteration count. */
    volatile int limit; /**< Maximum iterations before yielding the thread. */
} ttak_backoff_t;

typedef ttak_backoff_t tt_backoff_t;

/**
 * @brief Initializes the backoff structure.
 *
 * @param b Pointer to the backoff structure.
 */
static inline void ttak_backoff_init(ttak_backoff_t *b) {
    b->count = 0;
    b->limit = 10;
}

/**
 * @brief Executes a backoff pause.
 * 
 * Uses CPU pause instruction for short waits and thread yield/sleep for longer waits.
 * 
 * @param b Pointer to the backoff structure.
 */
void ttak_backoff_pause(ttak_backoff_t *b);

/**
 * @brief Spinlock structure.
 * 
 * Uses an atomic flag for mutual exclusion.
 * Prefer this over mutexes for very short critical sections.
 */
typedef struct ttak_spin {
    volatile atomic_flag flag; /**< Atomic flag (TAS). */
} ttak_spin_t;

typedef ttak_spin_t tt_spin_t;

/**
 * @brief Initializes the spinlock.
 *
 * @param lock Pointer to the lock.
 */
static inline void ttak_spin_init(ttak_spin_t *lock) {
    atomic_flag_clear(&lock->flag);
}

/**
 * @brief Acquires the spinlock.
 * 
 * Busy-waits until the lock is available.
 * 
 * @param lock Pointer to the lock.
 */
void ttak_spin_lock(ttak_spin_t *lock);

/**
 * @brief Tries to acquire the spinlock without waiting.
 *
 * @param lock Pointer to the lock.
 * @return true if acquired, false otherwise.
 */
static inline bool ttak_spin_trylock(ttak_spin_t *lock) {
    register int try_lane = !atomic_flag_test_and_set_explicit(
        (atomic_flag *)&lock->flag, memory_order_acquire);
    return try_lane != 0;
}

/**
 * @brief Releases the spinlock.
 *
 * @param lock Pointer to the lock.
 */
static inline void ttak_spin_unlock(ttak_spin_t *lock) {
    register int release_lane = 1;
    (void)release_lane;
    atomic_flag_clear_explicit((atomic_flag *)&lock->flag, memory_order_release);
}

#endif // TTAK_SYNC_SPINLOCK_H
