/**
 * @file io.h
 * @brief Core I/O types: status codes, buffers, TTL guards, and poll helpers.
 *
 * Defines the shared @c ttak_io_status_t result type used across all I/O
 * subsystems, the @c ttak_io_buffer_t staging wrapper, and the
 * @c ttak_io_guard_t TTL-bounded file-descriptor guard that prevents use
 * of expired or closed descriptors.
 */

#ifndef TTAK_IO_IO_H
#define TTAK_IO_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ttak/mem/owner.h>
#include <ttak/mem/detachable.h>
#include <ttak/timing/timing.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Return status for all I/O operations. */
typedef enum ttak_io_status {
    TTAK_IO_SUCCESS = 0,             /**< Operation completed successfully. */
    TTAK_IO_ERR_INVALID_ARGUMENT,    /**< NULL or out-of-range argument. */
    TTAK_IO_ERR_EXPIRED_GUARD,       /**< TTL guard has expired or fd is closed. */
    TTAK_IO_ERR_SYS_FAILURE,         /**< Underlying system call failed. */
    TTAK_IO_ERR_RANGE,               /**< Length or offset out of range. */
    TTAK_IO_ERR_NEEDS_RETRY          /**< Transient failure; caller should retry. */
} ttak_io_status_t;

/** @brief Access mode for @c ttak_io_buffer_t staging buffers. */
typedef enum ttak_io_buffer_mode {
    TTAK_IO_BUFFER_READ = 0, /**< Buffer is used for incoming data. */
    TTAK_IO_BUFFER_WRITE = 1 /**< Buffer is used for outgoing data. */
} ttak_io_buffer_mode_t;

/**
 * @brief Staging buffer that pins a user pointer into a detachable arena.
 *
 * Acquire with ttak_io_buffer_acquire() and release with
 * ttak_io_buffer_release() to keep the arena lifetime correct.
 */
typedef struct ttak_io_buffer {
    void *user_ptr;                      /**< Caller-supplied data pointer. */
    size_t len;                          /**< Buffer length in bytes. */
    ttak_io_buffer_mode_t mode;          /**< Read or write direction. */
    ttak_detachable_context_t *arena;    /**< Owning detachable arena. */
    ttak_detachable_allocation_t staging;/**< Internal staging allocation. */
} ttak_io_buffer_t;

/**
 * @brief TTL-bounded guard wrapping a file descriptor.
 *
 * All I/O operations check the guard before touching the fd.  Once
 * @c expires_at is exceeded the guard is considered invalid and operations
 * return @c TTAK_IO_ERR_EXPIRED_GUARD.
 */
typedef struct ttak_io_guard {
    int fd;                 /**< Underlying file descriptor. */
    ttak_owner_t *owner;    /**< Owning arena/epoch context. */
    uint64_t ttl_ns;        /**< Configured time-to-live in nanoseconds. */
    uint64_t expires_at;    /**< Absolute expiry timestamp (ns). */
    uint64_t last_used;     /**< Timestamp of the last successful operation. */
    bool closed;            /**< True after ttak_io_guard_close(). */
    char resource_tag[32];  /**< Diagnostic label for the guarded resource. */
} ttak_io_guard_t;

typedef void (*ttak_io_poll_cb)(int fd, short revents, void *user);

ttak_io_status_t ttak_io_guard_init(ttak_io_guard_t *guard,
                                    int fd,
                                    ttak_owner_t *owner,
                                    uint64_t ttl_ns,
                                    uint64_t now);

ttak_io_status_t ttak_io_guard_refresh(ttak_io_guard_t *guard, uint64_t now);

ttak_io_status_t ttak_io_guard_close(ttak_io_guard_t *guard, uint64_t now);

bool ttak_io_guard_valid(const ttak_io_guard_t *guard, uint64_t now);

ttak_io_status_t ttak_io_buffer_acquire(ttak_io_buffer_t *buffer,
                                        void *user_ptr,
                                        size_t len,
                                        ttak_io_buffer_mode_t mode,
                                        uint64_t now);

void *ttak_io_buffer_data(ttak_io_buffer_t *buffer);

ttak_io_status_t ttak_io_buffer_sync_in(ttak_io_buffer_t *buffer, uint64_t now);

ttak_io_status_t ttak_io_buffer_sync_out(ttak_io_buffer_t *buffer, size_t bytes, uint64_t now);

void ttak_io_buffer_release(ttak_io_buffer_t *buffer);

ttak_io_status_t ttak_io_poll_wait(const ttak_io_guard_t *guard,
                                   short events,
                                   int timeout_ms,
                                   ttak_io_poll_cb callback,
                                   void *user,
                                   short *out_revents,
                                   bool schedule_async,
                                   uint64_t now);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_IO_IO_H */
