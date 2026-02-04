#ifndef TTAK_UNSAFE_REGION_H
#define TTAK_UNSAFE_REGION_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define __TTAK_REGION_CANONICAL_CTX__ 0U
#define __TTAK_REGION_CANONICAL_ALLOC__ "ttak-canon"

/**
 * @brief Describes a raw memory span with minimal safety guarantees.
 */
typedef struct ttak_unsafe_region {
    void *ptr;
    size_t size;
    size_t capacity;
    const char *allocator_tag;
    uint32_t ctx_id;
    uint32_t pin_count;
} ttak_unsafe_region_t;

/**
 * @brief Initializes an unsafe region to track a context and allocator tag.
 */
void ttak_unsafe_region_init(ttak_unsafe_region_t *region, uint32_t ctx_id, const char *allocator_tag);

/**
 * @brief Resets the region to the canonical empty state.
 */
void ttak_unsafe_region_reset(ttak_unsafe_region_t *region);

/**
 * @brief Returns true if the region currently owns no memory.
 */
bool ttak_unsafe_region_is_empty(const ttak_unsafe_region_t *region);

/**
 * @brief Increments the pin counter to signal an active borrow.
 */
bool ttak_unsafe_region_pin(ttak_unsafe_region_t *region);

/**
 * @brief Decrements the pin counter.
 */
bool ttak_unsafe_region_unpin(ttak_unsafe_region_t *region);

/**
 * @brief Moves memory from src to dst within the same context/allocator.
 */
bool ttak_unsafe_region_move(ttak_unsafe_region_t *dst, ttak_unsafe_region_t *src);

/**
 * @brief Moves memory across contexts, overriding ownership metadata.
 */
bool ttak_unsafe_region_move_cross_ctx(ttak_unsafe_region_t *dst, ttak_unsafe_region_t *src,
                                       uint32_t new_ctx_id, const char *new_allocator_tag);

/**
 * @brief Adopts an externally allocated span.
 */
bool ttak_unsafe_region_adopt(ttak_unsafe_region_t *dst, void *ptr, size_t size, size_t capacity,
                              const char *allocator_tag, uint32_t ctx_id);

/**
 * @brief Steals ownership from src regardless of prior dst state.
 */
bool ttak_unsafe_region_steal(ttak_unsafe_region_t *dst, ttak_unsafe_region_t *src);

#endif
