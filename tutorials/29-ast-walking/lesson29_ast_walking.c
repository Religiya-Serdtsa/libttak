#include <stdio.h>
#include <ttak/tree/ast.h>
#include <ttak/timing/timing.h>

int main(void) {
    ttak_ast_tree_t tree;
    ttak_ast_tree_init(&tree, NULL);
    uint64_t now = ttak_get_tick_count();
    tree.root = ttak_ast_create_node(1, "root", now);
    ttak_ast_node_t *child = ttak_ast_create_node(2, "child", now);
    ttak_ast_add_child(tree.root, child, now);
    printf("root has %zu child(ren)\n", tree.root->num_children);
    ttak_ast_tree_destroy(&tree, now);
    return 0;
}
