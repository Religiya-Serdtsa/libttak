/**
 * @file sync.h
 * @brief Blocking (synchronous) I/O read/write wrappers.
 *
 * Thin wrappers around POSIX read/write that honour the TTL guard and
 * return typed status codes instead of raw errno values.
 */

#ifndef TTAK_IO_SYNC_H
#define TTAK_IO_SYNC_H

#include <stddef.h>
#include <stdint.h>

#include <ttak/io/io.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Performs a blocking read from the guarded file descriptor.
 *
 * @param guard      Initialised I/O guard.
 * @param dst        Destination buffer (at least @p len bytes).
 * @param len        Maximum bytes to read.
 * @param bytes_read Receives the actual number of bytes read (may be NULL).
 * @param now        Current monotonic timestamp in nanoseconds.
 * @return           @c TTAK_IO_SUCCESS on success, or an error code.
 */
ttak_io_status_t ttak_io_sync_read(ttak_io_guard_t *guard,
                                   void *dst,
                                   size_t len,
                                   size_t *bytes_read,
                                   uint64_t now);

/**
 * @brief Performs a blocking write to the guarded file descriptor.
 *
 * @param guard         Initialised I/O guard.
 * @param src           Source buffer.
 * @param len           Number of bytes to write.
 * @param bytes_written Receives the actual number of bytes written (may be NULL).
 * @param now           Current monotonic timestamp in nanoseconds.
 * @return              @c TTAK_IO_SUCCESS on success, or an error code.
 */
ttak_io_status_t ttak_io_sync_write(ttak_io_guard_t *guard,
                                    const void *src,
                                    size_t len,
                                    size_t *bytes_written,
                                    uint64_t now);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_IO_SYNC_H */
