/**
 * @file ntt.h
 * @brief Number-Theoretic Transform (NTT) and Montgomery arithmetic.
 *
 * Provides NTT-based polynomial multiplication over prime fields for
 * big-integer arithmetic.  Three built-in NTT primes are included, all
 * of the form p = c * 2^k + 1, supporting transforms up to 2^max_power_two
 * elements.  Chinese Remainder Theorem (CRT) reconstruction is provided to
 * lift results back to a larger ring.
 */

#ifndef TTAK_MATH_NTT_H
#define TTAK_MATH_NTT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <ttak/types/fixed.h>

/**
 * @brief Predefined prime information for NTT operations.
 */
typedef struct ttak_ntt_prime {
    uint64_t modulus;        /**< Prime modulus suitable for 2^k NTT. */
    uint64_t primitive_root; /**< Primitive root of unity for the modulus. */
    uint32_t max_power_two;  /**< Maximum supported log2 transform size. */
    uint64_t montgomery_inv; /**< -mod^{-1} mod 2^64 for Montgomery reduction. */
    uint64_t montgomery_r2;  /**< R^2 mod modulus for Montgomery conversion. */
} ttak_ntt_prime_t;

/** @brief Number of built-in NTT primes (three distinct Solinas-like primes). */
#define TTAK_NTT_PRIME_COUNT 3

/** @brief Table of built-in NTT-friendly primes. */
extern const ttak_ntt_prime_t ttak_ntt_primes[TTAK_NTT_PRIME_COUNT];

/** @brief Modular addition: returns (a + b) mod @p mod. */
uint64_t ttak_mod_add(uint64_t a, uint64_t b, uint64_t mod);
/** @brief Modular subtraction: returns (a - b) mod @p mod. */
uint64_t ttak_mod_sub(uint64_t a, uint64_t b, uint64_t mod);
/** @brief Modular multiplication: returns (a * b) mod @p mod. */
uint64_t ttak_mod_mul(uint64_t a, uint64_t b, uint64_t mod);
/** @brief Modular exponentiation: returns base^exp mod @p mod. */
uint64_t ttak_mod_pow(uint64_t base, uint64_t exp, uint64_t mod);
/** @brief Modular multiplicative inverse via extended Euclidean algorithm. */
uint64_t ttak_mod_inverse(uint64_t value, uint64_t mod);

/**
 * @brief Montgomery reduction from 128-bit product to field element.
 * @param value  128-bit input (a * b * R, where R = 2^64).
 * @param prime  Prime descriptor carrying the Montgomery constants.
 * @return       Reduced value in [0, prime->modulus).
 */
uint64_t ttak_montgomery_reduce(ttak_u128_t value, const ttak_ntt_prime_t *prime);

/** @brief Montgomery multiplication: returns (lhs * rhs * R^-1) mod p. */
uint64_t ttak_montgomery_mul(uint64_t lhs, uint64_t rhs, const ttak_ntt_prime_t *prime);

/** @brief Converts a field element into Montgomery form (multiplies by R^2). */
uint64_t ttak_montgomery_convert(uint64_t value, const ttak_ntt_prime_t *prime);

/**
 * @brief Performs a forward or inverse NTT on @p data in-place.
 *
 * @param data    Array of @p n field elements (must be a power of two).
 * @param n       Transform size (power of two, ≤ 2^prime->max_power_two).
 * @param prime   NTT prime providing the primitive root.
 * @param inverse True for inverse NTT (includes 1/n normalisation).
 * @return        True on success, false if @p n exceeds the prime's limit.
 */
_Bool ttak_ntt_transform(uint64_t *data, size_t n, const ttak_ntt_prime_t *prime, _Bool inverse);

/** @brief Point-wise multiplication: dst[i] = lhs[i] * rhs[i] mod p. */
void ttak_ntt_pointwise_mul(uint64_t *dst, const uint64_t *lhs, const uint64_t *rhs, size_t n, const ttak_ntt_prime_t *prime);

/** @brief Point-wise squaring: dst[i] = src[i]^2 mod p. */
void ttak_ntt_pointwise_square(uint64_t *dst, const uint64_t *src, size_t n, const ttak_ntt_prime_t *prime);

/** @brief Returns the smallest power of two ≥ @p value. */
size_t ttak_next_power_of_two(size_t value);

/**
 * @brief Single CRT residue term used by ttak_crt_combine().
 */
typedef struct ttak_crt_term {
    uint64_t residue; /**< Residue modulo @c modulus. */
    uint64_t modulus; /**< The modulus for this term. */
} ttak_crt_term_t;

/**
 * @brief Reconstructs a value from multiple CRT residues.
 *
 * @param terms        Array of @p count residue terms.
 * @param count        Number of terms.
 * @param residue_out  Receives the combined residue.
 * @param modulus_out  Receives the combined modulus (product of all moduli).
 * @return             True on success.
 */
_Bool ttak_crt_combine(const ttak_crt_term_t *terms, size_t count, ttak_u128_t *residue_out, ttak_u128_t *modulus_out);

#endif // TTAK_MATH_NTT_H
