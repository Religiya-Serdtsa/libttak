#include <stdio.h>
#include <ttak/container/pool.h>

int main(void) {
    ttak_object_pool_t *pool = ttak_object_pool_create(4, sizeof(int));
    if (!pool) {
        fputs("Failed to create pool. Did you build libttak?\n", stderr);
        return 1;
    }

    int *slot = (int *)ttak_object_pool_alloc(pool);
    if (!slot) {
        fputs("Pool unexpectedly full.\n", stderr);
        ttak_object_pool_destroy(pool);
        return 1;
    }

    *slot = 1234;
    printf("pooled value = %d\n", *slot);
    ttak_object_pool_free(pool, slot);
    ttak_object_pool_destroy(pool);
    return 0;
}
