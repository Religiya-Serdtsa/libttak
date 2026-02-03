#include <ttak/container/pool.h>
#include <ttak/sync/spinlock.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Creates pool logic.
 */
ttak_object_pool_t *ttak_object_pool_create(size_t capacity, size_t item_size) {
    ttak_object_pool_t *pool = malloc(sizeof(ttak_object_pool_t));
    if (!pool) return NULL;
    
    pool->buffer = calloc(capacity, item_size);
    pool->bitmap = calloc((capacity + 7) / 8, 1);
    pool->capacity = capacity;
    pool->item_size = item_size;
    pool->used_count = 0;
    ttak_spin_init(&pool->lock);
    
    if (!pool->buffer || !pool->bitmap) {
        free(pool->buffer);
        free(pool->bitmap);
        free(pool);
        return NULL;
    }
    return pool;
}

/**
 * @brief Destroys pool.
 */
void ttak_object_pool_destroy(ttak_object_pool_t *pool) {
    if (pool) {
        free(pool->buffer);
        free(pool->bitmap);
        free(pool);
    }
}

/**
 * @brief Fast allocation using bitmap scan.
 */
void *ttak_object_pool_alloc(ttak_object_pool_t *pool) {
    ttak_spin_lock(&pool->lock);
    if (pool->used_count >= pool->capacity) {
        ttak_spin_unlock(&pool->lock);
        return NULL;
    }
    
    // Naive linear scan (can be optimized)
    for (size_t i = 0; i < pool->capacity; i++) {
        if (!(pool->bitmap[i / 8] & (1 << (i % 8)))) {
            pool->bitmap[i / 8] |= (1 << (i % 8));
            pool->used_count++;
            ttak_spin_unlock(&pool->lock);
            return (char *)pool->buffer + (i * pool->item_size);
        }
    }
    ttak_spin_unlock(&pool->lock);
    return NULL;
}

/**
 * @brief Frees object by clearing bit.
 */
void ttak_object_pool_free(ttak_object_pool_t *pool, void *ptr) {
    if (!ptr) return;
    
    ttak_spin_lock(&pool->lock);
    ptrdiff_t offset = (char *)ptr - (char *)pool->buffer;
    
    // Bounds and alignment check
    if (offset < 0 || (size_t)offset >= (pool->capacity * pool->item_size) || (offset % pool->item_size) != 0) {
        ttak_spin_unlock(&pool->lock);
        return; // Invalid pointer
    }
    
    size_t idx = offset / pool->item_size;
    if (pool->bitmap[idx / 8] & (1 << (idx % 8))) {
        pool->bitmap[idx / 8] &= ~(1 << (idx % 8));
        pool->used_count--;
    }
    ttak_spin_unlock(&pool->lock);
}