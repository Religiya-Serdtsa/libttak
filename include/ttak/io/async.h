/**
 * @file async.h
 * @brief Non-blocking I/O read/write operations with completion callbacks.
 *
 * Wraps the platform poll/select mechanism to schedule asynchronous reads
 * and writes on a guarded file descriptor.  The callback is invoked on the
 * worker thread when the operation completes or times out.
 */

#ifndef TTAK_IO_ASYNC_H
#define TTAK_IO_ASYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ttak/io/io.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Completion callback invoked after an async I/O operation finishes.
 *
 *  @param status Final status of the operation.
 *  @param bytes  Number of bytes transferred.
 *  @param user   Caller-supplied context pointer.
 */
typedef void (*ttak_io_async_result_cb)(ttak_io_status_t status, size_t bytes, void *user);

/**
 * @brief Enqueues an asynchronous read from the guarded descriptor.
 *
 * @param guard      I/O guard wrapping the target fd.
 * @param dst        Destination buffer.
 * @param len        Maximum bytes to read.
 * @param timeout_ms Poll timeout in milliseconds (-1 = block indefinitely).
 * @param cb         Completion callback (may be NULL).
 * @param user       Context forwarded to @p cb.
 * @param now        Current monotonic timestamp in nanoseconds.
 * @return           @c TTAK_IO_SUCCESS if the read was enqueued, else error.
 */
ttak_io_status_t ttak_io_async_read(ttak_io_guard_t *guard,
                                    void *dst,
                                    size_t len,
                                    int timeout_ms,
                                    ttak_io_async_result_cb cb,
                                    void *user,
                                    uint64_t now);

/**
 * @brief Enqueues an asynchronous write to the guarded descriptor.
 *
 * @param guard      I/O guard wrapping the target fd.
 * @param src        Source buffer.
 * @param len        Number of bytes to write.
 * @param timeout_ms Poll timeout in milliseconds.
 * @param cb         Completion callback (may be NULL).
 * @param user       Context forwarded to @p cb.
 * @param now        Current monotonic timestamp in nanoseconds.
 * @return           @c TTAK_IO_SUCCESS if the write was enqueued, else error.
 */
ttak_io_status_t ttak_io_async_write(ttak_io_guard_t *guard,
                                     const void *src,
                                     size_t len,
                                     int timeout_ms,
                                     ttak_io_async_result_cb cb,
                                     void *user,
                                     uint64_t now);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_IO_ASYNC_H */
