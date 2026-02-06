#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <string.h>
#include <ttak/tree/btree.h>
#include <ttak/timing/timing.h>

static int cmp_keys(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

int main(void) {
    ttak_btree_t tree;
    ttak_btree_init(&tree, 2, cmp_keys, NULL, NULL);
    uint64_t now = ttak_get_tick_count();
    ttak_btree_insert(&tree, (void *)"gamma", (void *)"tri", now);
    void *value = ttak_btree_search(&tree, "gamma", now);
    printf("btree search -> %s\n", value ? (const char *)value : "missing");
    ttak_btree_destroy(&tree, now);
    return 0;
}
