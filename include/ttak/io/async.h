#ifndef TTAK_IO_ASYNC_H
#define TTAK_IO_ASYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ttak/io/io.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ttak_io_async_result_cb)(ttak_io_status_t status, size_t bytes, void *user);

ttak_io_status_t ttak_io_async_read(ttak_io_guard_t *guard,
                                    void *dst,
                                    size_t len,
                                    int timeout_ms,
                                    ttak_io_async_result_cb cb,
                                    void *user,
                                    uint64_t now);

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
