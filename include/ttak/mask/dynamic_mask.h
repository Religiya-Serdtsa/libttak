/**
 * @file dynamic_mask.h
 * @brief Dynamic bitmap utility with thread-safe access.
 * @author Gemini
 * @date 2026-02-08
 */

#ifndef TTAK_DYNAMIC_MASK_H
#define TTAK_DYNAMIC_MASK_H

#include <ttak/sync/sync.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @struct ttak_dynamic_mask_t
 * @brief Thread-safe dynamic bitmap.
 * Optimized for lock-free reads via EBR.
 */
typedef struct {
    uint64_t * _Atomic bits;    /**< The actual bitmap array (Atomic for lock-free read) */
    uint32_t capacity;          /**< Capacity in bits (multiples of 64) */
    uint32_t count;             /**< Number of set bits (optional tracking) */
    ttak_rwlock_t lock;         /**< RWLock for writer synchronization */
} ttak_dynamic_mask_t;

/**
 * @brief Initializes a dynamic mask.
 */
void ttak_dynamic_mask_init(ttak_dynamic_mask_t *mask);

/**
 * @brief Destroys a dynamic mask and frees memory.
 */
void ttak_dynamic_mask_destroy(ttak_dynamic_mask_t *mask);

/**
 * @brief Sets a bit at the given index, expanding the mask if necessary.
 * @return true if successful.
 */
bool ttak_dynamic_mask_set(ttak_dynamic_mask_t *mask, uint32_t bit_idx);

/**
 * @brief Clears a bit at the given index.
 */
void ttak_dynamic_mask_clear(ttak_dynamic_mask_t *mask, uint32_t bit_idx);

/**
 * @brief Checks if a bit is set at the given index.
 */
bool ttak_dynamic_mask_test(ttak_dynamic_mask_t *mask, uint32_t bit_idx);

/**
 * @brief Checks if a bit is set without taking a lock (use with external lock).
 */
bool ttak_dynamic_mask_test_unsafe(ttak_dynamic_mask_t *mask, uint32_t bit_idx);

/**
 * @brief Ensures the mask has capacity for the given bit index.
 */
bool ttak_dynamic_mask_ensure(ttak_dynamic_mask_t *mask, uint32_t bit_idx);

#endif /* TTAK_DYNAMIC_MASK_H */
