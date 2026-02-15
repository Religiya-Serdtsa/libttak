#include <ttak/mem/epoch_gc.h>
#include <ttak/mem/mem.h>
#include "test_macros.h"

static void test_epoch_gc_register_and_rotate(void) {
    ttak_epoch_gc_t gc;
    ttak_epoch_gc_init(&gc);

    void *ptr = ttak_mem_alloc(128, __TTAK_UNSAFE_MEM_FOREVER__, 0);
    ASSERT(ptr != NULL);

    ttak_epoch_gc_register(&gc, ptr, 128);
    gc.current_epoch = 1;
    ttak_epoch_gc_rotate(&gc);

    ttak_mem_free(ptr);
    ttak_epoch_gc_destroy(&gc);
}

int main(void) {
    RUN_TEST(test_epoch_gc_register_and_rotate);
    return 0;
}
