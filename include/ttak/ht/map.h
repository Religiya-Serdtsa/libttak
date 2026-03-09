/**
 * @file map.h
 * @brief Public API for the SoA open-addressing integer-keyed hash map.
 *
 * Wraps the lower-level @c ttak_map_t with convenience aliases so callers
 * can use either the verbose @c ttak_* names or the shorter @c tt_* aliases.
 */

#ifndef __TTAK_MAP_H__
#define __TTAK_MAP_H__

#include <stddef.h>
#include <stdint.h>
#include <ttak/ht/hash.h>

/** @brief Alias: create a new map with @p init_cap initial capacity. */
#define ttak_create_map tt_create_map
/** @brief Alias: insert key/value into the map. */
#define ttak_insert_to_map tt_ins_map
/** @brief Alias: look up a key in the map. */
#define ttak_map_get_key tt_map_get
/** @brief Alias: delete a key from the map. */
#define ttak_delete_from_map tt_del_map

/**
 * @brief Creates a new hash map with an initial capacity.
 *
 * @param init_cap Desired initial slot count (rounded up to power-of-two).
 * @param now      Current monotonic timestamp in nanoseconds.
 * @return         Pointer to the allocated map, or NULL on failure.
 */
tt_map_t *ttak_create_map(size_t init_cap, uint64_t now);

/**
 * @brief Inserts or updates a key/value pair.
 *
 * @param map Map to modify.
 * @param key Integer key.
 * @param val Value to associate with @p key.
 * @param now Current monotonic timestamp in nanoseconds.
 */
void ttak_insert_to_map(tt_map_t *map, uintptr_t key, size_t val, uint64_t now);

/**
 * @brief Removes a key from the map (no-op if not present).
 *
 * @param map Map to modify.
 * @param key Key to remove.
 * @param now Current monotonic timestamp in nanoseconds.
 */
void ttak_delete_from_map(tt_map_t *map, uintptr_t key, uint64_t now);

/**
 * @brief Looks up a key and writes its value to @p out.
 *
 * @param map Map to search.
 * @param key Key to find.
 * @param out Receives the associated value on success.
 * @param now Current monotonic timestamp in nanoseconds.
 * @return    Non-zero if the key was found.
 */
_Bool ttak_map_get_key(tt_map_t *map, uintptr_t key, size_t *out, uint64_t now);

/** @brief Trigger growth when load exceeds 1/3 of capacity. */
#define __TT_MAP_RESIZE__ 3
/** @brief Trigger shrink when load drops below 1/2 of capacity. */
#define __TT_MAP_SHRINK__ 2

#endif // __TTAK_MAP_H__
