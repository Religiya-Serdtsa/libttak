#include <ttak/mem/epoch_gc.h>
#include <ttak/mem/mem.h>
#include <ttak/mem_tree/mem_tree.h>
#include <stdatomic.h>
#include "test_macros.h"

/**
 * Test: register a block then destroy – GC owns the pointer and frees it.
 */
static void test_epoch_gc_register_and_destroy(void) {
    ttak_epoch_gc_t gc;
    ttak_epoch_gc_init(&gc);
    ttak_epoch_gc_manual_rotate(&gc, true);

    void *ptr = ttak_mem_alloc(128, __TTAK_UNSAFE_MEM_FOREVER__, 0);
    ASSERT(ptr != NULL);

    ttak_epoch_gc_register(&gc, ptr, 128);

    /* rotate should not free: ref_count is still 1 */
    atomic_store(&gc.current_epoch, 1);
    ttak_epoch_gc_rotate(&gc);

    /* destroy frees all remaining tracked memory – no manual ttak_mem_free */
    ttak_epoch_gc_destroy(&gc);
}

/**
 * Test: release ref_count to 0 so rotate actually cleans up the block.
 */
static void test_epoch_gc_rotate_cleanup(void) {
    ttak_epoch_gc_t gc;
    ttak_epoch_gc_init(&gc);
    ttak_epoch_gc_manual_rotate(&gc, true);

    void *ptr = ttak_mem_alloc(64, __TTAK_UNSAFE_MEM_FOREVER__, 0);
    ASSERT(ptr != NULL);

    ttak_epoch_gc_register(&gc, ptr, 64);

    /* release the node so ref_count drops to 0 */
    ttak_mem_node_t *node = ttak_mem_tree_find_node(&gc.tree, ptr);
    ASSERT(node != NULL);
    ttak_mem_node_release(node);

    /* rotate should now free the block (ref_count==0, expires_tick==0, now>=0) */
    ttak_epoch_gc_rotate(&gc);

    /* the pointer was freed during rotation – destroy has nothing left */
    ttak_epoch_gc_destroy(&gc);
}

int main(void) {
    RUN_TEST(test_epoch_gc_register_and_destroy);
    RUN_TEST(test_epoch_gc_rotate_cleanup);
    return 0;
}
