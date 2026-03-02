#include <ttak/ht/hash.h>
#include <ttak/ht/wyhash.h>
#include <ttak/security/siphash.h>

/**
 * @brief Compute the SipHash-2-4 digest for a machine-word key.
 *
 * @param key Input key to hash.
 * @param k0  First 64-bit SipHash key.
 * @param k1  Second 64-bit SipHash key.
 * @return 64-bit hash suitable for table indexing.
 */
uint64_t gen_hash_sip24(uintptr_t key, uint64_t k0, uint64_t k1) {
    return ttak_siphash24_u64((uint64_t)key, k0, k1);
}

/**
 * @brief Compute the wyhash digest for a machine-word key.
 *
 * @param key  Input key to hash.
 * @param seed Seed for hashing.
 * @return 64-bit hash suitable for map indexing.
 */
uint64_t gen_hash_wyhash(uintptr_t key, uint64_t seed) {
    return ttak_hash_u64((uint64_t)key, seed);
}
