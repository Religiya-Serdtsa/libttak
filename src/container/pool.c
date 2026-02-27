#include <ttak/container/pool.h>
#include <ttak/sync/spinlock.h>
#include <stdlib.h>
#include <string.h>

#define TTAK_POOL_OLS_ORDER        8U
#define TTAK_POOL_OLS_ORDER_LOG2   3U
#define TTAK_POOL_OLS_MASK         (TTAK_POOL_OLS_ORDER - 1U)
#define TTAK_POOL_OLS_AREA         (TTAK_POOL_OLS_ORDER * TTAK_POOL_OLS_ORDER)
#define TTAK_POOL_OLS_AREA_MASK    (TTAK_POOL_OLS_AREA - 1U)
#define TTAK_POOL_OLS_AREA_SHIFT   6U
#define TTAK_POOL_GF8_PRIMITIVE    0x3U /* x^3 + x + 1 polynomial */

#ifndef TTAK_POOL_HAVE_ARCH_OLS
#define TTAK_POOL_HAVE_ARCH_OLS 0
#endif

/* Generic GF(2^3) multiplier (order-8 finite field) */
static inline uint8_t ttak_pool_gf8_mul(uint8_t a, uint8_t b) {
    uint8_t product = 0;
    for (uint8_t i = 0; i < 3; ++i) {
        if (b & 1U) product ^= a;
        uint8_t carry = (uint8_t)(a & 0x4U);
        a <<= 1;
        if (carry) a ^= TTAK_POOL_GF8_PRIMITIVE;
        a &= TTAK_POOL_OLS_MASK;
        b >>= 1;
    }
    return product & TTAK_POOL_OLS_MASK;
}

#if TTAK_POOL_HAVE_ARCH_OLS
/* Architecture-specific backend must provide this symbol when enabled. */
extern uint8_t ttak_pool_arch_ols_lane(uint8_t lane_seed);
#define ttak_pool_ols_lane(lane_seed) ttak_pool_arch_ols_lane(lane_seed)
#else
static inline uint8_t ttak_pool_ols_lane(uint8_t lane_seed) {
    uint8_t row = lane_seed & TTAK_POOL_OLS_MASK;
    uint8_t col = (lane_seed >> TTAK_POOL_OLS_ORDER_LOG2) & TTAK_POOL_OLS_MASK;
    uint8_t latin_a = row ^ col;
    uint8_t latin_b = ttak_pool_gf8_mul(TTAK_POOL_GF8_PRIMITIVE, row) ^ col;
    return (uint8_t)((latin_a << TTAK_POOL_OLS_ORDER_LOG2) | latin_b);
}
#endif

static inline uint8_t ttak_pool_ols_lane_advance(uint8_t lane_seed, uint8_t stride) {
    return (uint8_t)((lane_seed + stride) & TTAK_POOL_OLS_AREA_MASK);
}

static inline size_t ttak_pool_chunk_count(size_t capacity) {
    size_t tiles = (capacity + (TTAK_POOL_OLS_AREA - 1U)) >> TTAK_POOL_OLS_AREA_SHIFT;
    return tiles ? tiles : 1U;
}

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
    pool->ols_chunk_count = ttak_pool_chunk_count(capacity);
    pool->ols_chunk_cursor = 0;
    pool->ols_lane_guard = 1U;
    pool->ols_lane_seed = pool->ols_lane_guard;
    pool->ols_lane_stride = 9U; /* Coprime to 64 to cover the entire lattice. */
    pool->last_recycled_index = SIZE_MAX;
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

    if (pool->last_recycled_index != SIZE_MAX) {
        size_t idx = pool->last_recycled_index;
        size_t byte = idx >> 3;
        uint8_t mask = (uint8_t)(1U << (idx & 7U));
        if (idx < pool->capacity && !(pool->bitmap[byte] & mask)) {
            pool->bitmap[byte] |= mask;
            pool->used_count++;
            pool->last_recycled_index = SIZE_MAX;
            ttak_spin_unlock(&pool->lock);
            return (char *)pool->buffer + (idx * pool->item_size);
        }
        pool->last_recycled_index = SIZE_MAX;
    }

    uint8_t lane = pool->ols_lane_seed;
    size_t chunk = pool->ols_chunk_cursor;
    const uint8_t stride = pool->ols_lane_stride;
    const uint8_t guard = pool->ols_lane_guard;
    const size_t chunk_count = pool->ols_chunk_count ? pool->ols_chunk_count : 1U;

    size_t probed = 0;
    while (probed < pool->capacity) {
        size_t idx = (chunk << TTAK_POOL_OLS_AREA_SHIFT) + ttak_pool_ols_lane(lane);
        lane = ttak_pool_ols_lane_advance(lane, stride);
        if (lane == guard) {
            chunk++;
            if (chunk >= chunk_count) chunk = 0;
        }

        if (idx >= pool->capacity) {
            ++probed;
            continue;
        }

        size_t byte = idx >> 3;
        uint8_t mask = (uint8_t)(1U << (idx & 7U));
        if (!(pool->bitmap[byte] & mask)) {
            pool->bitmap[byte] |= mask;
            pool->used_count++;
            pool->ols_lane_seed = lane;
            pool->ols_chunk_cursor = chunk;
            ttak_spin_unlock(&pool->lock);
            return (char *)pool->buffer + (idx * pool->item_size);
        }
        ++probed;
    }

    pool->ols_lane_seed = lane;
    pool->ols_chunk_cursor = chunk;
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
        pool->last_recycled_index = idx;
    }
    ttak_spin_unlock(&pool->lock);
}
