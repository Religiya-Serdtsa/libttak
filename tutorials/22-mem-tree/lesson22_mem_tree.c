#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>

#include <ttak/mem/mem.h>
#include <ttak/mem_tree/mem_tree.h>
#include <ttak/timing/timing.h>

int main(void) {
    ttak_mem_tree_t tree;
    ttak_mem_tree_init(&tree);

    const uint64_t tick = ttak_get_tick_count_ns();
    int *payload = ttak_mem_alloc(sizeof(*payload), __TTAK_UNSAFE_MEM_FOREVER__, tick);
    if (payload) {
        *payload = 42;
        uint64_t now = ttak_get_tick_count();
        ttak_mem_node_t *node = ttak_mem_tree_add(&tree, payload, sizeof(*payload), now + 1000, true);
        if (node) {
            printf("tracking pointer %p\n", node->ptr);
            ttak_mem_tree_remove(&tree, node);
            ttak_mem_free(payload);
        }
    }
    ttak_mem_tree_destroy(&tree);
    return 0;
}
