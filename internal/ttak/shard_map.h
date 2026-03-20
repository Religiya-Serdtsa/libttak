/**
 * @file shard_map.h
 * @brief Deterministic shard mapping utilities for the concurrent async runtime.
 *
 * Implements a deterministic, array-based computation model for routing task
 * hashes to shard indices.  The mapping pipeline is:
 *
 *   task_hash  →  (row, col) coordinates  →  routing table lookup  →  shard index
 *
 * The routing table is pre-computed as an 8×8 Latin square, guaranteeing that
 * every shard appears exactly once in each row and each column.  This provides
 * uniform dispersion similar in spirit to Orthogonal Latin Square constructions,
 * minimising clustering and collision across shards.
 *
 * All functions are deterministic, pure, and side-effect-free.
 */

#ifndef TTAK_INTERNAL_SHARD_MAP_H
#define TTAK_INTERNAL_SHARD_MAP_H

#include <stdint.h>
#include <stddef.h>

/** Default shard count — must be a power of two, maximum 8. */
#define TTAK_POOL_SHARD_COUNT 8

/** Log₂ of TTAK_POOL_SHARD_COUNT (used for bit shifts). */
#define TTAK_POOL_SHARD_LOG2  3

/** Mask to reduce a coordinate to [0, TTAK_POOL_SHARD_COUNT). */
#define TTAK_POOL_SHARD_MASK  (TTAK_POOL_SHARD_COUNT - 1)

/**
 * @brief 8×8 Latin-square routing table.
 *
 * shard_route_table[row][col] gives the shard index for a task whose
 * hash coordinates are (row, col).  Each value 0-7 appears exactly once
 * per row and once per column, ensuring uniform distribution and avoiding
 * hot-shard clustering for structured hash inputs.
 *
 * Construction: entry (r, c) = (r + c) % 8  (cyclic Latin square).
 */
static const uint8_t shard_route_table[TTAK_POOL_SHARD_COUNT][TTAK_POOL_SHARD_COUNT] = {
    /* col: 0  1  2  3  4  5  6  7 */
    /* r=0 */ { 0, 1, 2, 3, 4, 5, 6, 7 },
    /* r=1 */ { 1, 2, 3, 4, 5, 6, 7, 0 },
    /* r=2 */ { 2, 3, 4, 5, 6, 7, 0, 1 },
    /* r=3 */ { 3, 4, 5, 6, 7, 0, 1, 2 },
    /* r=4 */ { 4, 5, 6, 7, 0, 1, 2, 3 },
    /* r=5 */ { 5, 6, 7, 0, 1, 2, 3, 4 },
    /* r=6 */ { 6, 7, 0, 1, 2, 3, 4, 5 },
    /* r=7 */ { 7, 0, 1, 2, 3, 4, 5, 6 },
};

/**
 * @brief Convert a task hash into (row, col) shard coordinates.
 *
 * Uses Fibonacci/multiplicative hashing with the 64-bit golden-ratio constant
 * to project the hash into a bounded 2D coordinate space.  The upper and lower
 * halves of the mixed value are used independently to decorrelate the two axes.
 *
 * @param hash  Task hash value.
 * @param row   Output: row coordinate in [0, TTAK_POOL_SHARD_COUNT).
 * @param col   Output: column coordinate in [0, TTAK_POOL_SHARD_COUNT).
 */
static inline void ttak_shard_hash_to_coords(uint64_t hash,
                                              uint32_t *row,
                                              uint32_t *col)
{
    /* 64-bit golden-ratio constant (⌊2^64 / φ⌋) for Fibonacci hashing */
    const uint64_t GOLDEN = UINT64_C(0x9e3779b97f4a7c15);
    uint64_t mixed = hash * GOLDEN;
    /* Upper 32 bits → row; lower 32 bits → col.
     * Both are masked to [0, TTAK_POOL_SHARD_COUNT). */
    *row = (uint32_t)(mixed >> 32) & (uint32_t)TTAK_POOL_SHARD_MASK;
    *col = (uint32_t)(mixed & UINT32_MAX) & (uint32_t)TTAK_POOL_SHARD_MASK;
}

/**
 * @brief Map a task hash to a shard index via the 2D routing table.
 *
 * This is the primary deterministic mapping function.  Given any 64-bit
 * hash the output is always in [0, TTAK_POOL_SHARD_COUNT) and is
 * reproducible across runs.
 *
 * @param hash  Task identifier hash.
 * @return      Shard index in [0, TTAK_POOL_SHARD_COUNT).
 */
static inline size_t ttak_shard_for_hash(uint64_t hash)
{
    uint32_t row, col;
    ttak_shard_hash_to_coords(hash, &row, &col);
    return (size_t)shard_route_table[row][col];
}

/**
 * @brief Map a worker index to its preferred shard.
 *
 * Workers are spread across shards using modular arithmetic so that the
 * initial affinity assignment is uniform regardless of thread count.
 *
 * @param worker_idx  Zero-based worker index.
 * @return            Preferred shard index.
 */
static inline size_t ttak_shard_for_worker(size_t worker_idx)
{
    return worker_idx & (size_t)TTAK_POOL_SHARD_MASK;
}

#endif /* TTAK_INTERNAL_SHARD_MAP_H */
