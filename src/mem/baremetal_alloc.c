/* baremetal_alloc.c - In-house static-pool allocator for EMBEDDED_BAREMETAL */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BAREMETAL_HEAP_SIZE (64 * 1024)

static uint8_t g_baremetal_heap[BAREMETAL_HEAP_SIZE];
static int g_baremetal_heap_initialized = 0;

typedef struct block_header {
    size_t size;
    int used;
    struct block_header *next;
} block_header_t;

static block_header_t *g_heap_head = NULL;

static void baremetal_heap_init(void) {
    g_heap_head = (block_header_t *)(uintptr_t)g_baremetal_heap;
    g_heap_head->size = BAREMETAL_HEAP_SIZE - sizeof(block_header_t);
    g_heap_head->used = 0;
    g_heap_head->next = NULL;
}

void *malloc(size_t size) {
    if (!g_baremetal_heap_initialized) {
        baremetal_heap_init();
        g_baremetal_heap_initialized = 1;
    }
    if (size == 0) return NULL;
    size = (size + 7) & ~7;

    block_header_t *curr = g_heap_head;
    while (curr) {
        if (!curr->used && curr->size >= size) {
            if (curr->size >= size + sizeof(block_header_t) + 8) {
                block_header_t *new_block = (block_header_t *)((uint8_t *)(curr + 1) + size);
                new_block->size = curr->size - size - sizeof(block_header_t);
                new_block->used = 0;
                new_block->next = curr->next;
                curr->next = new_block;
                curr->size = size;
            }
            curr->used = 1;
            return (void *)(curr + 1);
        }
        curr = curr->next;
    }
    return NULL;
}

void free(void *ptr) {
    if (!ptr) return;
    block_header_t *block = (block_header_t *)ptr - 1;
    block->used = 0;

    if (block->next && !block->next->used) {
        block->size += sizeof(block_header_t) + block->next->size;
        block->next = block->next->next;
    }

    block_header_t *curr = g_heap_head;
    block_header_t *prev = NULL;
    while (curr && curr != block) {
        prev = curr;
        curr = curr->next;
    }
    if (prev && !prev->used) {
        prev->size += sizeof(block_header_t) + block->size;
        prev->next = block->next;
    }
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    block_header_t *block = (block_header_t *)ptr - 1;
    if (block->size >= size) return ptr;
    void *p = malloc(size);
    if (p) {
        memcpy(p, ptr, block->size);
        free(ptr);
    }
    return p;
}

void abort(void) {
    while (1) {
        __asm volatile ("bkpt #0");
    }
}

static unsigned long g_rand_seed = 1;

int rand(void) {
    g_rand_seed = g_rand_seed * 1103515245UL + 12345UL;
    return (int)((g_rand_seed / 65536UL) % 32768UL);
}

void srand(unsigned int seed) {
    g_rand_seed = seed;
}

/* --- mols_control stub (src/net excluded in bare-metal) --- */
#include <ttak/mols_control.h>
uint32_t ttak_apply_mols_control(uint16_t node_id, uint32_t current_load) {
    (void)node_id;
    return current_load;
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
    if (nmemb <= 1) return;
    char *arr = (char *)base;
    char *tmp = malloc(size);
    if (!tmp) return;
    for (size_t i = 0; i < nmemb - 1; i++) {
        for (size_t j = 0; j < nmemb - i - 1; j++) {
            if (compar(arr + j * size, arr + (j + 1) * size) > 0) {
                memcpy(tmp, arr + j * size, size);
                memcpy(arr + j * size, arr + (j + 1) * size, size);
                memcpy(arr + (j + 1) * size, tmp, size);
            }
        }
    }
    free(tmp);
}
