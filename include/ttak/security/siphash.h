#ifndef TTAK_SECURITY_SIPHASH_H
#define TTAK_SECURITY_SIPHASH_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief SipHash-2-4 implementation for arbitrary byte arrays.
 * 
 * SipHash is a cryptographically strong PRF (pseudorandom function)
 * optimized for short messages.
 *
 * @param key     Input data to hash.
 * @param len     Length of the input data.
 * @param k0      First 64-bit key.
 * @param k1      Second 64-bit key.
 * @return 64-bit hash value.
 */
uint64_t ttak_siphash24(const void *key, size_t len, uint64_t k0, uint64_t k1);

/**
 * @brief Compute SipHash-2-4 for a single 64-bit word.
 *
 * @param val     64-bit value to hash.
 * @param k0      First 64-bit key.
 * @param k1      Second 64-bit key.
 * @return 64-bit hash value.
 */
uint64_t ttak_siphash24_u64(uint64_t val, uint64_t k0, uint64_t k1);

#endif // TTAK_SECURITY_SIPHASH_H
