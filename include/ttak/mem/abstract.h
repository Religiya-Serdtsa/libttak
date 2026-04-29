/**
 * @file abstract.h
 * @brief Pointer-stable logical memory with segmented backing and scoped maps.
 *
 * This API exposes a stable logical handle while allowing the implementation to
 * manage the physical backing as one or more internal segments.
 *
 * Core model:
 * - The handle returned by ttak_abstract_alloc() is the stable identity.
 * - Logical bytes may be backed by multiple internal segments.
 * - Callers can use read/write helpers or temporarily map a logical window.
 * - A mapped window is contiguous from the caller's perspective even when the
 *   implementation must stage it behind the scenes.
 */
#ifndef TTAK_MEM_ABSTRACT_H
#define TTAK_MEM_ABSTRACT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque pointer-stable logical memory object. */
typedef struct ttak_abstract_mem ttak_abstract_mem_t;

typedef enum ttak_abstract_access {
    TTAK_ABSTRACT_ACCESS_READ = 0,
    TTAK_ABSTRACT_ACCESS_WRITE = 1,
} ttak_abstract_access_t;

/**
 * @brief Scoped mapping of a logical byte window in an abstract object.
 *
 * A map pins relocation behind the object's synchronization boundary and
 * exposes a contiguous byte window for direct indexed access. The mapping is
 * valid only until ttak_abstract_unmap() is called.
 */
typedef struct ttak_abstract_map {
    void *data;
    size_t size;
    size_t offset;
    const ttak_abstract_mem_t *mem;
    ttak_abstract_access_t access;
    void *opaque;
    uint32_t flags;
} ttak_abstract_map_t;

/**
 * @brief Allocate a pointer-stable abstract memory object.
 *
 * The returned pointer is a stable logical handle, not a direct backing pointer.
 * The logical object starts with @p size bytes, zero-initialized.
 *
 * @param size Logical byte length to allocate.
 * @return Stable handle on success, NULL on failure.
 */
ttak_abstract_mem_t *ttak_abstract_alloc(size_t size);

/**
 * @brief Free an abstract memory object.
 *
 * Behavior:
 * - Clears and releases current backing region.
 * - Releases internal metadata and synchronization resources.
 * - Safe with NULL (no-op).
 *
 * @param mem Handle returned by ttak_abstract_alloc().
 */
void ttak_abstract_free(ttak_abstract_mem_t *mem);

/**
 * @brief Get current logical size in bytes.
 * @param mem Handle.
 * @return Logical size, or 0 when mem is NULL.
 */
size_t ttak_abstract_size(const ttak_abstract_mem_t *mem);

/**
 * @brief Copy out data from logical memory.
 *
 * This function is relocation-safe because it snapshots under a read lock.
 *
 * @param mem Handle.
 * @param offset Start offset in logical bytes.
 * @param out Destination buffer.
 * @param len Number of bytes to copy.
 * @return 0 on success, -1 on invalid input/range.
 */
int ttak_abstract_read(const ttak_abstract_mem_t *mem, size_t offset, void *out, size_t len);

/**
 * @brief Copy in data to logical memory.
 *
 * This function may trigger internal relocation/compaction when capacity needs
 * adjustment or periodic maintenance conditions are met.
 *
 * @param mem Handle.
 * @param offset Start offset in logical bytes.
 * @param src Source buffer.
 * @param len Number of bytes to copy.
 * @return 0 on success, -1 on invalid input/range/failure.
 */
int ttak_abstract_write(ttak_abstract_mem_t *mem, size_t offset, const void *src, size_t len);

/**
 * @brief Change logical size and preserve existing prefix data.
 *
 * Resize semantics:
 * - Growing zero-fills new logical range.
 * - Shrinking keeps prefix and may trigger compaction relocation.
 * - Backing replacement uses disjoint-region publication rules.
 *
 * @param mem Handle.
 * @param new_size New logical byte size.
 * @return 0 on success, -1 on failure.
 */
int ttak_abstract_resize(ttak_abstract_mem_t *mem, size_t new_size);

/**
 * @brief Force an internal relocation cycle for maintenance/compaction.
 *
 * Useful when the caller wants deterministic relocation points.
 *
 * @param mem Handle.
 * @return 0 on success, -1 on failure.
 */
int ttak_abstract_compact(ttak_abstract_mem_t *mem);

/**
 * @brief Map a logical byte window for direct access.
 *
 * The map stays valid until ttak_abstract_unmap() is called. READ maps allow
 * inspection; WRITE maps additionally permit mutation and block relocation for
 * the duration of the mapping.
 *
 * @param mem Handle.
 * @param offset Start offset in logical bytes.
 * @param len Number of bytes requested.
 * @param access Requested access mode.
 * @param map Output map token.
 * @return 0 on success, -1 on invalid input/range.
 */
int ttak_abstract_map(ttak_abstract_mem_t *mem, size_t offset, size_t len,
                      ttak_abstract_access_t access,
                      ttak_abstract_map_t *map);

/**
 * @brief Release a previously mapped logical window.
 *
 * Safe to call on a zero-initialized map. Resets the token to its canonical
 * empty state.
 *
 * @param map Map token returned by ttak_abstract_map().
 */
void ttak_abstract_unmap(ttak_abstract_map_t *map);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_MEM_ABSTRACT_H */
