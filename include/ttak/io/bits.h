#ifndef TTAK_IO_BITS_H
#define TTAK_IO_BITS_H

#include <stddef.h>
#include <stdint.h>

#include <ttak/io/io.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t ttak_io_bits_fnv32(const void *data, size_t len);

ttak_io_status_t ttak_io_bits_verify(const void *data,
                                     size_t len,
                                     uint32_t expected_checksum);

ttak_io_status_t ttak_io_bits_recover(const void *snapshot,
                                      size_t len,
                                      void *dst,
                                      uint32_t expected_checksum);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_IO_BITS_H */
