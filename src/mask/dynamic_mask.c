/**
 * @file dynamic_mask.c
 * @brief Implementation of the dynamic bitmap utility.
 */

#include <ttak/mask/dynamic_mask.h>
#include <stdlib.h>
#include <string.h>

void ttak_dynamic_mask_init(ttak_dynamic_mask_t *mask) {
    if (!mask) return;
    mask->bits = NULL;
    mask->capacity = 0;
    mask->count = 0;
    ttak_rwlock_init(&mask->lock);
}

void ttak_dynamic_mask_destroy(ttak_dynamic_mask_t *mask) {
    if (!mask) return;
    ttak_rwlock_wrlock(&mask->lock);
    free(mask->bits);
    mask->bits = NULL;
    mask->capacity = 0;
    ttak_rwlock_unlock(&mask->lock);
    ttak_rwlock_destroy(&mask->lock);
}

bool ttak_dynamic_mask_ensure(ttak_dynamic_mask_t *mask, uint32_t bit_idx) {
    if (bit_idx < mask->capacity) return true;

    uint32_t new_cap = ((bit_idx / 64) + 1) * 64;
    size_t new_size = (new_cap / 64) * sizeof(uint64_t);
    
    uint64_t *new_bits = realloc(mask->bits, new_size);
    if (!new_bits) return false;

    /* Zero out new memory */
    size_t old_size = (mask->capacity / 64) * sizeof(uint64_t);
    memset((uint8_t*)new_bits + old_size, 0, new_size - old_size);

    mask->bits = new_bits;
    mask->capacity = new_cap;
    return true;
}

bool ttak_dynamic_mask_set(ttak_dynamic_mask_t *mask, uint32_t bit_idx) {
    ttak_rwlock_wrlock(&mask->lock);
    
    if (!ttak_dynamic_mask_ensure(mask, bit_idx)) {
        ttak_rwlock_unlock(&mask->lock);
        return false;
    }

    uint32_t idx = bit_idx / 64;
    uint64_t bit = 1ULL << (bit_idx % 64);

    if (!(mask->bits[idx] & bit)) {
        mask->bits[idx] |= bit;
        mask->count++;
    }

    ttak_rwlock_unlock(&mask->lock);
    return true;
}

void ttak_dynamic_mask_clear(ttak_dynamic_mask_t *mask, uint32_t bit_idx) {
    ttak_rwlock_wrlock(&mask->lock);
    
    if (bit_idx < mask->capacity) {
        uint32_t idx = bit_idx / 64;
        uint64_t bit = 1ULL << (bit_idx % 64);
        if (mask->bits[idx] & bit) {
            mask->bits[idx] &= ~bit;
            mask->count--;
        }
    }

    ttak_rwlock_unlock(&mask->lock);
}

bool ttak_dynamic_mask_test_unsafe(ttak_dynamic_mask_t *mask, uint32_t bit_idx) {
    if (bit_idx >= mask->capacity) return false;
    return (mask->bits[bit_idx / 64] & (1ULL << (bit_idx % 64))) != 0;
}

bool ttak_dynamic_mask_test(ttak_dynamic_mask_t *mask, uint32_t bit_idx) {
    ttak_rwlock_rdlock(&mask->lock);
    bool res = ttak_dynamic_mask_test_unsafe(mask, bit_idx);
    ttak_rwlock_unlock(&mask->lock);
    return res;
}
