/**
 * @file abstract.h
 * @brief Pointer-stable abstract memory built on relocatable VMA-backed regions.
 *
 * This API provides a "logical pointer" abstraction for callers that want stable
 * object identity while allowing the runtime to move backing storage internally.
 *
 * Core model:
 * - The handle pointer returned by ttak_abstract_alloc() is the stable identity.
 * - The byte region behind that identity is movable and may be replaced.
 * - Callers must use read/write helpers, not raw backing pointers.
 *
 * Why this exists:
 * - Control fragmentation and compaction internally.
 * - Keep user-visible identity stable.
 * - Allow memcpy-based migration and old-region scrubbing.
 *
 * Relocation safety policy:
 * - New backing is always allocated as a disjoint virtual region.
 * - Data is copied old->new with memcpy.
 * - Publication of the new region is atomic.
 * - Old region is explicitly zero-filled before unmap/release.
 *
 * Concurrency policy:
 * - No epoch scheme is used.
 * - Read/write API calls are synchronized with rwlock barriers.
 * - Relocation is serialized with writers and blocks concurrent readers briefly.
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

#ifdef __cplusplus
}
#endif

#endif /* TTAK_MEM_ABSTRACT_H */
