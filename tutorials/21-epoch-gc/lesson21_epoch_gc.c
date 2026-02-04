#include <stdio.h>
#include <stdlib.h>
#include <ttak/mem/epoch_gc.h>

int main(void) {
    ttak_epoch_gc_t gc;
    ttak_epoch_gc_init(&gc);
    int *value = malloc(sizeof(*value));
    if (value) {
        *value = 7;
        ttak_epoch_gc_register(&gc, value, sizeof(*value));
        puts("registered allocation with epoch GC");
    }
    ttak_epoch_gc_rotate(&gc);
    ttak_epoch_gc_destroy(&gc);
    return 0;
}
