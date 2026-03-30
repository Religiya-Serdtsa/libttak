#include <ttak/mem/detachable.h>
#include "test_macros.h"

static void test_detached_mode_requires_epoch(void) {
    ttak_detachable_context_t ctx;
    ttak_detachable_context_init(&ctx, TTAK_ARENA_USE_LOCKED_ACCESS);

    ttak_detach_status_reset(&ctx.base_status);
    ctx.base_status.bits = TTAK_DETACHABLE_DETACH_NOCHECK | TTAK_DETACHABLE_STATUS_KNOWN;

    ttak_detachable_allocation_t alloc = ttak_detachable_mem_alloc(&ctx, 8, 1);
    ASSERT(alloc.data != NULL);
    ASSERT((alloc.detach_status.bits & TTAK_DETACHABLE_DETACH_NOCHECK) == 0);
    ASSERT((alloc.detach_status.bits & TTAK_DETACHABLE_ATTACH) != 0);

    ttak_detachable_mem_free(&ctx, &alloc);
    ttak_detachable_context_destroy(&ctx);
}

static void test_detached_mode_stays_separate_from_standard(void) {
    ttak_detachable_context_t ctx;
    ttak_detachable_context_init(&ctx, TTAK_ARENA_HAS_EPOCH_RECLAMATION);

    ttak_detach_status_reset(&ctx.base_status);
    ctx.base_status.bits = TTAK_DETACHABLE_DETACH_NOCHECK | TTAK_DETACHABLE_STATUS_KNOWN;

    ttak_detachable_allocation_t alloc = ttak_detachable_mem_alloc(&ctx, 8, 7);
    ASSERT(alloc.data != NULL);
    ASSERT((alloc.detach_status.bits & TTAK_DETACHABLE_DETACH_NOCHECK) != 0);
    ASSERT((alloc.detach_status.bits & TTAK_DETACHABLE_ATTACH) == 0);

    ttak_detachable_mem_free(&ctx, &alloc);
    ttak_detachable_context_destroy(&ctx);
}

int main(void) {
    RUN_TEST(test_detached_mode_requires_epoch);
    RUN_TEST(test_detached_mode_stays_separate_from_standard);
    return 0;
}
