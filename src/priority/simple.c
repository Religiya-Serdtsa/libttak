#include <ttak/priority/simple.h>
#include <ttak/mem/mem.h>
#include <stddef.h>

/* Queue Implementation */

/**
 * @brief Initialize a simple pointer queue.
 *
 * @param q Queue to initialize.
 */
void ttak_simple_queue_init(ttak_simple_queue_t *q) {
    if (!q) return;
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

/**
 * @brief Push an element to the tail of the queue.
 *
 * @param q    Queue to update.
 * @param data Data pointer to store.
 * @param now  Timestamp for allocator bookkeeping.
 */
void ttak_simple_queue_push(ttak_simple_queue_t *q, void *data, uint64_t now) {
    if (!q) return;
    ttak_simple_node_t *node = (ttak_simple_node_t *)ttak_mem_alloc(sizeof(ttak_simple_node_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!node) return;

    node->data = data;
    node->next = NULL;

    if (q->tail) {
        // Access tail to be safe, though we just own it.
        if (ttak_mem_access(q->tail, now)) {
            q->tail->next = node;
        } else {
            // Tail is invalid? This should not happen if we own the queue.
            // But if it does, we recover by resetting or just appending.
            // Let's assume queue integrity is maintained.
            // If tail is invalid, we might be in a bad state.
            // We'll treat it as empty for safety.
             q->head = node;
        }
    } else {
        q->head = node;
    }
    q->tail = node;
    q->size++;
}

/**
 * @brief Pop the head element from the queue.
 *
 * @param q   Queue to update.
 * @param now Timestamp for pointer validation.
 * @return Stored data pointer or NULL when empty.
 */
void *ttak_simple_queue_pop(ttak_simple_queue_t *q, uint64_t now) {
    if (!q || !q->head) return NULL;

    ttak_simple_node_t *node = q->head;
    if (!ttak_mem_access(node, now)) {
        // Head is invalid. Potentially corruption.
        // We can't trust the queue structure.
        return NULL;
    }

    void *data = node->data;
    q->head = node->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    q->size--;

    ttak_mem_free(node);
    return data;
}

/**
 * @brief Check whether the queue has no elements.
 *
 * @param q Queue to inspect.
 * @return true if empty, false otherwise.
 */
bool ttak_simple_queue_is_empty(const ttak_simple_queue_t *q) {
    return !q || q->head == NULL;
}

/**
 * @brief Return the number of enqueued nodes.
 *
 * @param q Queue to inspect.
 * @return Element count.
 */
size_t ttak_simple_queue_size(const ttak_simple_queue_t *q) {
    return q ? q->size : 0;
}

/**
 * @brief Destroy the queue and free all nodes.
 *
 * @param q   Queue to destroy.
 * @param now Timestamp for deallocation.
 */
void ttak_simple_queue_destroy(ttak_simple_queue_t *q, uint64_t now) {
    if (!q) return;
    while (q->head) {
        ttak_simple_queue_pop(q, now);
    }
}

/* Stack Implementation */

/**
 * @brief Initialize a simple LIFO stack.
 *
 * @param s Stack to initialize.
 */
void ttak_simple_stack_init(ttak_simple_stack_t *s) {
    if (!s) return;
    s->top = NULL;
    s->size = 0;
}

/**
 * @brief Push an element onto the stack.
 *
 * @param s    Stack to update.
 * @param data Pointer to store.
 * @param now  Timestamp for allocator bookkeeping.
 */
void ttak_simple_stack_push(ttak_simple_stack_t *s, void *data, uint64_t now) {
    if (!s) return;
    ttak_simple_node_t *node = (ttak_simple_node_t *)ttak_mem_alloc(sizeof(ttak_simple_node_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!node) return;

    node->data = data;
    node->next = s->top;
    s->top = node;
    s->size++;
}

/**
 * @brief Pop the top element from the stack.
 *
 * @param s   Stack to update.
 * @param now Timestamp for pointer validation.
 * @return Stored data pointer or NULL when empty.
 */
void *ttak_simple_stack_pop(ttak_simple_stack_t *s, uint64_t now) {
    if (!s || !s->top) return NULL;

    ttak_simple_node_t *node = s->top;
    if (!ttak_mem_access(node, now)) {
        return NULL;
    }

    void *data = node->data;
    s->top = node->next;
    s->size--;

    ttak_mem_free(node);
    return data;
}

/**
 * @brief Check whether the stack contains no elements.
 *
 * @param s Stack to inspect.
 * @return true if empty, false otherwise.
 */
bool ttak_simple_stack_is_empty(const ttak_simple_stack_t *s) {
    return !s || s->top == NULL;
}

/**
 * @brief Return the number of nodes stored in the stack.
 *
 * @param s Stack to inspect.
 * @return Element count.
 */
size_t ttak_simple_stack_size(const ttak_simple_stack_t *s) {
    return s ? s->size : 0;
}

/**
 * @brief Destroy the stack by freeing all nodes.
 *
 * @param s   Stack to destroy.
 * @param now Timestamp for deallocation.
 */
void ttak_simple_stack_destroy(ttak_simple_stack_t *s, uint64_t now) {
    if (!s) return;
    while (s->top) {
        ttak_simple_stack_pop(s, now);
    }
}
