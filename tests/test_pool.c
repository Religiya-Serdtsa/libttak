#include <ttak/container/pool.h>
#include "test_macros.h"

static void test_object_pool_reuses_slots(void) {
    ttak_object_pool_t *pool = ttak_object_pool_create(10, 64);
    void *p1 = ttak_object_pool_alloc(pool);
    void *p2 = ttak_object_pool_alloc(pool);

    ASSERT(p1 != NULL);
    ASSERT(p2 != NULL);
    ASSERT(p1 != p2);

    ttak_object_pool_free(pool, p1);
    void *p3 = ttak_object_pool_alloc(pool);
    ASSERT(p3 == p1);

    ttak_object_pool_destroy(pool);
}

int main(void) {
    RUN_TEST(test_object_pool_reuses_slots);
    return 0;
}
