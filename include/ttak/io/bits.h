/**
 * @file bits.h
 * @brief Lightweight FNV-32 checksum utilities for buffer integrity checks.
 *
 * Provides a fast non-cryptographic checksum, a verifier, and a recovery
 * helper that copies a snapshot only when its checksum matches.
 */

#ifndef TTAK_IO_BITS_H
#define TTAK_IO_BITS_H

#include <stddef.h>
#include <stdint.h>

#include <ttak/io/io.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Computes a 32-bit FNV-1a checksum over a byte buffer.
 *
 * @param data Pointer to the data to checksum.
 * @param len  Number of bytes.
 * @return     32-bit FNV-1a digest.
 */
uint32_t ttak_io_bits_fnv32(const void *data, size_t len);

/**
 * @brief Verifies that a buffer's FNV-32 checksum matches an expected value.
 *
 * @param data              Buffer to verify.
 * @param len               Buffer length in bytes.
 * @param expected_checksum Expected FNV-32 digest.
 * @return                  @c TTAK_IO_SUCCESS if matching, else error.
 */
ttak_io_status_t ttak_io_bits_verify(const void *data,
                                     size_t len,
                                     uint32_t expected_checksum);

/**
 * @brief Copies @p snapshot into @p dst only when the checksum is valid.
 *
 * @param snapshot          Source snapshot buffer.
 * @param len               Buffer length in bytes.
 * @param dst               Destination to copy into on success.
 * @param expected_checksum Expected FNV-32 digest of @p snapshot.
 * @return                  @c TTAK_IO_SUCCESS if checksum matched and copy done.
 */
ttak_io_status_t ttak_io_bits_recover(const void *snapshot,
                                      size_t len,
                                      void *dst,
                                      uint32_t expected_checksum);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_IO_BITS_H */
