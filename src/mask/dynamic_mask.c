/**
 * @file dynamic_mask.c
 * @brief Implementation of the dynamic bitmap utility.
 */

#include <ttak/mask/dynamic_mask.h>
#include <ttak/mem/mem.h>
#include <ttak/mem/epoch.h>
#include <ttak/types/ttak_compiler.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

void ttak_dynamic_mask_init(ttak_dynamic_mask_t *mask) {
    if (TTAK_UNLIKELY(!mask)) return;
    mask->capacity = 64;
    mask->count = 0;
    uint64_t *initial = (uint64_t *)ttak_dangerous_calloc(1, sizeof(uint64_t));
    atomic_init(&mask->bits, initial);
    ttak_rwlock_init(&mask->lock);
}

void ttak_dynamic_mask_destroy(ttak_dynamic_mask_t *mask) {
    if (TTAK_UNLIKELY(!mask)) return;
    uint64_t *ptr = atomic_load(&mask->bits);
    if (ptr) ttak_dangerous_free(ptr);
    ttak_rwlock_destroy(&mask->lock);
}

bool ttak_dynamic_mask_ensure(ttak_dynamic_mask_t *mask, uint32_t bit_idx) {
    if (TTAK_LIKELY(bit_idx < mask->capacity)) return true;

    uint32_t new_cap = mask->capacity ? mask->capacity : 64;
    while (bit_idx >= new_cap) new_cap *= 2;

    uint64_t *new_bits = (uint64_t *)ttak_dangerous_calloc(new_cap / 64, sizeof(uint64_t));
    if (TTAK_UNLIKELY(!new_bits)) return false;

    uint64_t *old_bits = atomic_load(&mask->bits);
    if (TTAK_LIKELY(old_bits)) {
        memcpy(new_bits, old_bits, (mask->capacity / 64) * sizeof(uint64_t));
    }

    mask->capacity = new_cap;
    atomic_store_explicit(&mask->bits, new_bits, memory_order_release);
    
    if (TTAK_LIKELY(old_bits)) {
        ttak_epoch_retire(old_bits, ttak_dangerous_free);
    }

    return true;
}

bool ttak_dynamic_mask_set(ttak_dynamic_mask_t *mask, uint32_t bit_idx) {
    ttak_rwlock_wrlock(&mask->lock);
    if (TTAK_UNLIKELY(!ttak_dynamic_mask_ensure(mask, bit_idx))) {
        ttak_rwlock_unlock(&mask->lock);
        return false;
    }

    uint64_t *bits = atomic_load(&mask->bits);
    uint32_t word = bit_idx / 64;
    uint32_t bit = bit_idx % 64;

    if (!(bits[word] & (1ULL << bit))) {
        bits[word] |= (1ULL << bit);
        mask->count++;
    }

    ttak_rwlock_unlock(&mask->lock);
    return true;
}

void ttak_dynamic_mask_clear(ttak_dynamic_mask_t *mask, uint32_t bit_idx) {
    ttak_rwlock_wrlock(&mask->lock);
    if (TTAK_UNLIKELY(bit_idx >= mask->capacity)) {
        ttak_rwlock_unlock(&mask->lock);
        return;
    }

    uint64_t *bits = atomic_load(&mask->bits);
    uint32_t word = bit_idx / 64;
    uint32_t bit = bit_idx % 64;

    if (bits[word] & (1ULL << bit)) {
        bits[word] &= ~(1ULL << bit);
        mask->count--;
    }

    ttak_rwlock_unlock(&mask->lock);
}

bool ttak_dynamic_mask_test_unsafe(ttak_dynamic_mask_t *mask, uint32_t bit_idx) {
    if (TTAK_UNLIKELY(bit_idx >= mask->capacity)) return false;
    uint64_t *bits = atomic_load(&mask->bits);
    return (bits[bit_idx / 64] & (1ULL << (bit_idx % 64))) != 0;
}

bool ttak_dynamic_mask_test(ttak_dynamic_mask_t *mask, uint32_t bit_idx) {
    /* [LOCK-FREE PATH] */
    uint64_t *bits = atomic_load_explicit(&mask->bits, memory_order_acquire);
    uint32_t cap = mask->capacity; 
    
    if (TTAK_UNLIKELY(bit_idx >= cap)) return false;
    return (bits[bit_idx / 64] & (1ULL << (bit_idx % 64))) != 0;
}
