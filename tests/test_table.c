#include "test_macros.h"
#include <ttak/ht/table.h>
#include <string.h>
#include <stdint.h>

int key_cmp_str(const void *k1, const void *k2) {
    return strcmp((const char *)k1, (const char *)k2);
}

void test_table_basic() {
    ttak_table_t tbl;
    ttak_table_init(&tbl, 16, NULL, key_cmp_str, NULL, NULL);
    uint64_t now = 1000;

    ttak_table_put(&tbl, "key1", 4, "val1", now);
    ttak_table_put(&tbl, "key2", 4, "val2", now);
    ttak_table_put(&tbl, "key3", 4, "val3", now);

    ASSERT(tbl.size == 3);
    ASSERT(strcmp((char *)ttak_table_get(&tbl, "key1", 4, now), "val1") == 0);
    ASSERT(strcmp((char *)ttak_table_get(&tbl, "key2", 4, now), "val2") == 0);
    ASSERT(strcmp((char *)ttak_table_get(&tbl, "key3", 4, now), "val3") == 0);

    ttak_table_put(&tbl, "key2", 4, "val2_new", now);
    ASSERT(tbl.size == 3);
    ASSERT(strcmp((char *)ttak_table_get(&tbl, "key2", 4, now), "val2_new") == 0);

    ttak_table_remove(&tbl, "key1", 4, now);
    ASSERT(tbl.size == 2);
    ASSERT(ttak_table_get(&tbl, "key1", 4, now) == NULL);

    ttak_table_destroy(&tbl, now);
}

void test_table_resize() {
    ttak_table_t tbl;
    ttak_table_init(&tbl, 4, NULL, key_cmp_str, NULL, NULL);
    uint64_t now = 1000;

    char keys[100][16];
    char vals[100][16];

    for (int i = 0; i < 100; i++) {
        sprintf(keys[i], "key%d", i);
        sprintf(vals[i], "val%d", i);
        ttak_table_put(&tbl, keys[i], strlen(keys[i]), vals[i], now);
    }

    ASSERT(tbl.size == 100);
    for (int i = 0; i < 100; i++) {
        char *v = ttak_table_get(&tbl, keys[i], strlen(keys[i]), now);
        ASSERT(v != NULL);
        ASSERT(strcmp(v, vals[i]) == 0);
    }

    ttak_table_destroy(&tbl, now);
}

int main() {
    RUN_TEST(test_table_basic);
    RUN_TEST(test_table_resize);
    return 0;
}
