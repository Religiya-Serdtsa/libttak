#ifndef TTAK_MEM_EPOCH_GC_H
#define TTAK_MEM_EPOCH_GC_H

#include <ttak/mem_tree/mem_tree.h>

/**
 * @brief Epoch-based garbage collection context.
 * 
 * Manages memory lifecycle using generational epochs.
 * Designed for periodic, non-blocking cleanup where the user triggers the cycle.
 * Traverses the heap tree to identify and free expired blocks without a global stop-the-world pause.
 */
typedef struct ttak_epoch_gc {
    ttak_mem_tree_t tree;       /**< Underlying memory tree tracking allocations. */
    uint64_t current_epoch;     /**< Current active epoch ID. */
    uint64_t last_cleanup_ts;   /**< Timestamp of the last cleanup execution. */
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

#endif // TTAK_MEM_EPOCH_GC_H