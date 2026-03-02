#ifndef __TTAK_HASH_H__
#define __TTAK_HASH_H__

#include <stdint.h>
#include <stddef.h>
#include <stdalign.h>
#include <ttak/types/ttak_align.h>

#define EMPTY    0x00
#define DELETED  0xDE
#define OCCUPIED 0x0C

/**
 * @brief Map structure using Structure of Arrays (SoA) for cache efficiency.
 */
typedef struct {
    uint8_t   *ctrls;  /**< Control bytes (OCCUPIED, EMPTY, DELETED) */
    uintptr_t *keys;   /**< Keys array */
    size_t    *values; /**< Values array */
    size_t    cap;     /**< Capacity (must be power of two) */
    size_t    size;    /**< Number of occupied slots */
    uint64_t  seed;    /**< Seed for wyhash */
#ifndef _MSC_VER
    alignas(ttak_max_align_t) char padding[0];
#endif
} ttak_map_t;

typedef ttak_map_t tt_map_t;

uint64_t gen_hash_sip24(uintptr_t key, uint64_t k0, uint64_t k1);
uint64_t gen_hash_wyhash(uintptr_t key, uint64_t seed);

#endif // __TTAK_HASH_H__
