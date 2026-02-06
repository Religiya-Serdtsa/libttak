#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <string.h>
#include <ttak/tree/bplus.h>
#include <ttak/timing/timing.h>

static int cmp_strings(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

int main(void) {
    ttak_bplus_tree_t tree;
    ttak_bplus_init(&tree, 3, cmp_strings, NULL, NULL);
    uint64_t now = ttak_get_tick_count();
    ttak_bplus_insert(&tree, (void *)"alpha", (void *)"one", now);
    void *value = ttak_bplus_get(&tree, "alpha", now);
    printf("lookup alpha -> %s\n", value ? (const char *)value : "missing");
    ttak_bplus_destroy(&tree, now);
    return 0;
}
