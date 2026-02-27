#ifndef TTAK_MEM_EPOCH_GC_H
#define TTAK_MEM_EPOCH_GC_H

#include <ttak/mem_tree/mem_tree.h>
#include <pthread.h>
#include <stdatomic.h>

/**
 * @brief Epoch-based garbage collection context.
 * 
 * Manages memory lifecycle using generational epochs.
 * Designed for periodic, non-blocking cleanup where the user triggers the cycle.
 * Traverses the heap tree to identify and free expired blocks without a global stop-the-world pause.
 */
typedef struct ttak_epoch_gc {
    ttak_mem_tree_t tree;             /**< Underlying memory tree tracking allocations. */
    _Atomic uint64_t current_epoch;   /**< Current active epoch ID. */
    _Atomic uint64_t last_cleanup_ts; /**< Timestamp of the last cleanup execution. */
    pthread_t rotate_thread;          /**< Background thread handling automatic epoch rotation. */
    pthread_mutex_t rotate_lock;      /**< Synchronization primitive for thread wake/sleep. */
    pthread_cond_t rotate_cond;       /**< Condition variable to nudge the rotate thread. */
    _Atomic _Bool shutdown_requested; /**< Signals rotate thread to exit. */
    _Atomic _Bool manual_rotation;    /**< True when the user wants manual rotation mode. */
    _Atomic uint64_t min_rotate_ns;   /**< Minimum wait interval (ns) between auto rotations. */
    _Atomic uint64_t max_rotate_ns;   /**< Maximum backoff interval (ns) between auto rotations. */
    _Bool rotate_thread_started;      /**< Tracks whether the rotate thread was launched. */
} ttak_epoch_gc_t;

typedef ttak_epoch_gc_t tt_epoch_gc_t;

/**
 * @brief Initializes the Epoch GC.
 * 
 * @param gc Pointer to the GC structure.
 */
void ttak_epoch_gc_init(ttak_epoch_gc_t *gc);

/**
 * @brief Destroys the GC context and frees tracked resources.
 * 
 * @param gc Pointer to the GC structure.
 */
void ttak_epoch_gc_destroy(ttak_epoch_gc_t *gc);

/**
 * @brief Registers a pointer to be managed by the current epoch.
 * 
 * @param gc Pointer to the GC structure.
 * @param ptr Pointer to the allocated memory.
 * @param size Size of the memory block.
 */
void ttak_epoch_gc_register(ttak_epoch_gc_t *gc, void *ptr, size_t size);

/**
 * @brief Advances the epoch and triggers a non-blocking cleanup pass.
 * 
 * This should be called periodically by the user. It increments the epoch
 * and traverses the heap tree to free blocks associated with expired epochs
 * (based on configured retention or explicit lifetime).
 * 
 * @param gc Pointer to the GC structure.
 */
void ttak_epoch_gc_rotate(ttak_epoch_gc_t *gc);

/**
 * @brief Enables or disables the automatic rotation worker.
 *
 * When manual mode is enabled, callers are responsible for invoking
 * ttak_epoch_gc_rotate() (and ttak_epoch_reclaim()) explicitly.
 *
 * @param gc Pointer to the GC structure.
 * @param manual_mode True to disable the auto-rotator, false to re-enable it.
 */
void ttak_epoch_gc_manual_rotate(ttak_epoch_gc_t *gc, _Bool manual_mode);

#endif // TTAK_MEM_EPOCH_GC_H
