#include <ttak/tree/ast.h>
#include <ttak/mem/mem.h>
#include <stdlib.h>

/**
 * @brief Initialize an AST container with an optional value destructor.
 *
 * @param tree       Tree to initialize.
 * @param free_value Callback used to free node values.
 */
void ttak_ast_tree_init(ttak_ast_tree_t *tree, void (*free_value)(void*)) {
    if (!tree) return;
    tree->root = NULL;
    tree->free_value = free_value;
}

/**
 * @brief Allocate a new AST node.
 *
 * @param type  Application-specific node type.
 * @param value Payload pointer stored on the node.
 * @param now   Timestamp for allocator bookkeeping.
 * @return Pointer to the node or NULL on allocation failure.
 */
ttak_ast_node_t *ttak_ast_create_node(int type, void *value, uint64_t now) {
    ttak_ast_node_t *node = (ttak_ast_node_t *)ttak_mem_alloc(sizeof(ttak_ast_node_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!node) return NULL;
    
    node->type = type;
    node->value = value;
    node->children = NULL;
    node->num_children = 0;
    node->cap_children = 0;
    node->parent = NULL;
    
    return node;
}

/**
 * @brief Append a child node to the parent.
 *
 * @param parent Parent node.
 * @param child  Child node to attach.
 * @param now    Timestamp for potential reallocations.
 */
void ttak_ast_add_child(ttak_ast_node_t *parent, ttak_ast_node_t *child, uint64_t now) {
    if (!parent || !child) return;
    
    // Check if we need to resize children array
    if (parent->num_children >= parent->cap_children) {
        size_t new_cap = (parent->cap_children == 0) ? 4 : parent->cap_children * 2;
        ttak_ast_node_t **new_children = (ttak_ast_node_t **)ttak_mem_realloc(
            parent->children, 
            sizeof(ttak_ast_node_t *) * new_cap, 
            __TTAK_UNSAFE_MEM_FOREVER__, 
            now
        );
        if (!new_children) return;
        parent->children = new_children;
        parent->cap_children = new_cap;
    }
    
    parent->children[parent->num_children++] = child;
    child->parent = parent;
}

/**
 * @brief Recursively destroy a node and its descendants.
 *
 * @param node       Node to destroy.
 * @param free_value Callback that frees node values.
 * @param now        Timestamp for memory operations.
 */
static void recursive_destroy_node(ttak_ast_node_t *node, void (*free_value)(void*), uint64_t now) {
    if (!node) return;
    
    if (ttak_mem_access(node, now)) {
        for (size_t i = 0; i < node->num_children; i++) {
            recursive_destroy_node(node->children[i], free_value, now);
        }
        
        if (node->children) {
            ttak_mem_free(node->children);
        }
        
        if (free_value && node->value) {
            free_value(node->value);
        }
        
        ttak_mem_free(node);
    }
}

/**
 * @brief Destroy the entire AST.
 *
 * @param tree Tree to destroy.
 * @param now  Timestamp for memory bookkeeping.
 */
void ttak_ast_tree_destroy(ttak_ast_tree_t *tree, uint64_t now) {
    if (!tree || !tree->root) return;
    recursive_destroy_node(tree->root, tree->free_value, now);
    tree->root = NULL;
}
