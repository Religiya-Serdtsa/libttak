#ifndef TTAK_IO_SYNC_H
#define TTAK_IO_SYNC_H

#include <stddef.h>
#include <stdint.h>

#include <ttak/io/io.h>

#ifdef __cplusplus
extern "C" {
#endif

ttak_io_status_t ttak_io_sync_read(ttak_io_guard_t *guard,
                                   void *dst,
                                   size_t len,
                                   size_t *bytes_read,
                                   uint64_t now);

ttak_io_status_t ttak_io_sync_write(ttak_io_guard_t *guard,
                                    const void *src,
                                    size_t len,
                                    size_t *bytes_written,
                                    uint64_t now);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_IO_SYNC_H */
