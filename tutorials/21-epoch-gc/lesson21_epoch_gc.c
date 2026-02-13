#define _XOPEN_SOURCE 700
#include <stdio.h>

#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>
#include <ttak/mem/epoch_gc.h>

int main(void) {
    ttak_epoch_gc_t gc;

    uint64_t now = ttak_get_tick_count_ns();
    ttak_epoch_gc_init(&gc);
    int *value = (int *)ttak_mem_alloc(sizeof(*value), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (value) {
        *value = 7;
        ttak_epoch_gc_register(&gc, value, sizeof(*value));
        puts("registered allocation with epoch GC");
    }
    ttak_epoch_gc_rotate(&gc);
    ttak_epoch_gc_destroy(&gc);
    return 0;
}
