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

static void test_flip_hot_path_bypasses_cache(void) {
    ttak_detachable_context_t ctx;
    ttak_detachable_context_init(&ctx, TTAK_ARENA_USE_LOCKED_ACCESS);
    ctx.flip_event_threshold = 1;
    ctx.flip_window_ns = 1000000000ULL;

    ttak_detachable_allocation_t alloc1 = ttak_detachable_mem_alloc(&ctx, 8, 0);
    ASSERT(alloc1.data != NULL);
    ttak_detachable_mem_free(&ctx, &alloc1);

    ttak_detachable_allocation_t alloc2 = ttak_detachable_mem_alloc(&ctx, 8, 0);
    ASSERT(alloc2.data != NULL);
    ttak_detachable_mem_free(&ctx, &alloc2);

    ASSERT(ctx.small_cache.count <= 1);
    ttak_detachable_context_destroy(&ctx);
}

int main(void) {
    RUN_TEST(test_detached_mode_requires_epoch);
    RUN_TEST(test_detached_mode_stays_separate_from_standard);
    RUN_TEST(test_flip_hot_path_bypasses_cache);
    return 0;
}
