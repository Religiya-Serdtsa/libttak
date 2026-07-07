#include <ttak/ht/map.h>
#include <stddef.h>
#include <stdint.h>
#include "test_macros.h"

static void test_map_null_handles(void) {
    uint64_t now = 500;
    size_t out = 0;

    /* These should all be no-ops on a NULL map. */
    ttak_insert_to_map(NULL, 1, 100, now);
    ttak_delete_from_map(NULL, 1, now);
    ASSERT(ttak_map_get_key(NULL, 1, &out, now) == 0);
    ttak_destroy_map(NULL);
}

static void test_map_basic(void) {
    uint64_t now = 500;
    tt_map_t *map = ttak_create_map(16, now);
    ASSERT(map != NULL);

    size_t out = 0;
    ASSERT(ttak_map_get_key(map, 123, &out, now) == 0);

    ttak_insert_to_map(map, 123, 456, now);
    ASSERT(ttak_map_get_key(map, 123, &out, now) != 0);
    ASSERT(out == 456);

    ttak_delete_from_map(map, 123, now);
    ASSERT(ttak_map_get_key(map, 123, &out, now) == 0);

    ttak_destroy_map(map);
}

static void test_map_update(void) {
    uint64_t now = 500;
    tt_map_t *map = ttak_create_map(16, now);
    ASSERT(map != NULL);

    size_t out = 0;
    ttak_insert_to_map(map, 7, 10, now);
    ttak_insert_to_map(map, 7, 20, now);
    ASSERT(ttak_map_get_key(map, 7, &out, now) != 0);
    ASSERT(out == 20);

    ttak_destroy_map(map);
}

static void test_map_multiple_keys(void) {
    uint64_t now = 500;
    tt_map_t *map = ttak_create_map(16, now);
    ASSERT(map != NULL);

    for (uintptr_t i = 1; i <= 50; ++i) {
        ttak_insert_to_map(map, i, (size_t)(i * 2), now);
    }

    for (uintptr_t i = 1; i <= 50; ++i) {
        size_t out = 0;
        ASSERT(ttak_map_get_key(map, i, &out, now) != 0);
        ASSERT(out == (size_t)(i * 2));
    }

    /* Delete every other key and verify removal. */
    for (uintptr_t i = 1; i <= 50; i += 2) {
        ttak_delete_from_map(map, i, now);
    }
    for (uintptr_t i = 1; i <= 50; i += 2) {
        size_t out = 0;
        ASSERT(ttak_map_get_key(map, i, &out, now) == 0);
    }
    for (uintptr_t i = 2; i <= 50; i += 2) {
        size_t out = 0;
        ASSERT(ttak_map_get_key(map, i, &out, now) != 0);
        ASSERT(out == (size_t)(i * 2));
    }

    ttak_destroy_map(map);
}

int main(void) {
    RUN_TEST(test_map_null_handles);
    RUN_TEST(test_map_basic);
    RUN_TEST(test_map_update);
    RUN_TEST(test_map_multiple_keys);
    return 0;
}
