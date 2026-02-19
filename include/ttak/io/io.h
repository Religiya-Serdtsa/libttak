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

typedef enum ttak_io_status {
    TTAK_IO_SUCCESS = 0,
    TTAK_IO_ERR_INVALID_ARGUMENT,
    TTAK_IO_ERR_EXPIRED_GUARD,
    TTAK_IO_ERR_SYS_FAILURE,
    TTAK_IO_ERR_RANGE,
    TTAK_IO_ERR_NEEDS_RETRY
} ttak_io_status_t;

typedef enum ttak_io_buffer_mode {
    TTAK_IO_BUFFER_READ = 0,
    TTAK_IO_BUFFER_WRITE = 1
} ttak_io_buffer_mode_t;

typedef struct ttak_io_buffer {
    void *user_ptr;
    size_t len;
    ttak_io_buffer_mode_t mode;
    ttak_detachable_context_t *arena;
    ttak_detachable_allocation_t staging;
} ttak_io_buffer_t;

typedef struct ttak_io_guard {
    int fd;
    ttak_owner_t *owner;
    uint64_t ttl_ns;
    uint64_t expires_at;
    uint64_t last_used;
    bool closed;
    char resource_tag[32];
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
