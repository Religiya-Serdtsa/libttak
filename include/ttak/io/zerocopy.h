#ifndef TTAK_IO_ZEROCOPY_H
#define TTAK_IO_ZEROCOPY_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <ttak/io/io.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TTAK_IO_ZC_MAX_IOV             4U
#define TTAK_IO_ZC_SEG_SHIFT           12U
#define TTAK_IO_ZC_SEG_BYTES           (1U << TTAK_IO_ZC_SEG_SHIFT)
#define TTAK_IO_ZC_SEG_MASK            (TTAK_IO_ZC_SEG_BYTES - 1U)
#define TTAK_IO_ZC_MAX_SEGMENTS        256U
#define TTAK_IO_ZC_SEGMENT_WORD_BITS   32U
#define TTAK_IO_ZC_SEGMENT_WORD_COUNT  \
    ((TTAK_IO_ZC_MAX_SEGMENTS + TTAK_IO_ZC_SEGMENT_WORD_BITS - 1U) / \
     TTAK_IO_ZC_SEGMENT_WORD_BITS)

/**
 * @brief Temporary zero-copy receive window allocated in a detachable arena.
 *
 * Data is allocated as one contiguous buffer but internally tracked as
 * power-of-two segments. segment_mask marks which segments are populated so the
 * recv loop can advance using bit operations.
 */
typedef struct ttak_io_zerocopy_region {
    const uint8_t *data;
    size_t len;
    size_t capacity;
    bool read_only;
    uint32_t segment_mask[TTAK_IO_ZC_SEGMENT_WORD_COUNT];
    ttak_detachable_context_t *arena;
    ttak_detachable_allocation_t allocation;
} ttak_io_zerocopy_region_t;

/**
 * @brief Initializes a region so it can be reused across recv operations.
 */
void ttak_io_zerocopy_region_init(ttak_io_zerocopy_region_t *region);

/**
 * @brief Receives data from @p fd into a detachable buffer without copying back
 *        into user memory.
 *
 * The caller owns @p fd lifetime. On success, @p region->data points to a
 * read-only buffer that remains valid until ttak_io_zerocopy_release().
 */
ttak_io_status_t ttak_io_zerocopy_recv_fd(int fd,
                                          ttak_io_zerocopy_region_t *region,
                                          size_t max_len,
                                          int flags,
                                          uint64_t now);

/**
 * @brief Releases resources captured during recv.
 */
void ttak_io_zerocopy_release(ttak_io_zerocopy_region_t *region);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_IO_ZEROCOPY_H */
