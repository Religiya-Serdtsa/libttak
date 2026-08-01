/**
 * @file security_util.h
 * @brief Small security utilities shared by the crypto modules.
 */

#ifndef TTAK_SECURITY_SECURITY_UTIL_H
#define TTAK_SECURITY_SECURITY_UTIL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Overwrites a memory region with zeros in a way that resists
 *        compiler dead-store elimination.
 *
 * Use this to clear keys, key schedules, hash contexts, and other sensitive
 * intermediate data.  The write is performed through a volatile pointer so
 * the store cannot be optimised away even when the buffer is not read again.
 *
 * @param ptr Pointer to the first byte to clear.
 * @param len Number of bytes to clear.
 */
void ttak_secure_zero(void *ptr, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_SECURITY_SECURITY_UTIL_H */
