#include <ttak/mem/epoch_gc.h>
#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>

/**
 * @brief Initializes the Epoch GC structure.
 * 
 * Sets up the underlying memory tree with manual cleanup mode enabled,
 * allowing the user to control the garbage collection cycles via ttak_epoch_gc_rotate.
 * 
 * @param gc Pointer to the GC context.
 */
void ttak_epoch_gc_init(ttak_epoch_gc_t *gc) {
    ttak_mem_tree_init(&gc->tree);
    // Enable manual cleanup to prevent automatic background threads from interfering
    // with the user-controlled periodic cycle.
    ttak_mem_tree_set_manual_cleanup(&gc->tree, true);
    gc->current_epoch = 0;
    gc->last_cleanup_ts = ttak_get_tick_count();
}

/**
 * @brief Destroys the GC context.
 * 
 * Frees all remaining memory blocks tracked by the tree.
 * 
 * @param gc Pointer to the GC context.
 */
void ttak_epoch_gc_destroy(ttak_epoch_gc_t *gc) {
    ttak_mem_tree_destroy(&gc->tree);
}

/**
 * @brief Registers a memory block with the current epoch.
 * 
 * @param gc Pointer to the GC context.
 * @param ptr Pointer to the memory block.
 * @param size Size of the block.
 */
void ttak_epoch_gc_register(ttak_epoch_gc_t *gc, void *ptr, size_t size) {
    // Add to the underlying mem_tree.
    // The 'expires_tick' (4th arg) is 0 because we manage lifetime via epochs,
    // not strictly by clock time, though mem_tree supports both.
    // We treat these as "roots" for the current epoch.
    ttak_mem_tree_add(&gc->tree, ptr, size, 0, true);
}

/**
 * @brief Rotates the epoch and performs cleanup.
 * 
 * Increments the epoch counter and triggers the memory tree's cleanup logic.
 * This function iterates through the tree, identifying blocks that are no longer
 * referenced or valid, and frees them. It is designed to be called periodically.
 * 
 * @param gc Pointer to the GC context.
 */
void ttak_epoch_gc_rotate(ttak_epoch_gc_t *gc) {
    gc->current_epoch++;
    
    // Perform cleanup using the current timestamp.
    // mem_tree_perform_cleanup iterates the tree and removes expired/unreferenced nodes.
    // By controlling when this is called, we avoid "stop-the-world" pauses at arbitrary times.
    ttak_mem_tree_perform_cleanup(&gc->tree, ttak_get_tick_count());
    
    gc->last_cleanup_ts = ttak_get_tick_count();
}