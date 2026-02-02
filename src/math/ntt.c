#include <ttak/math/ntt.h>
#include <stdint.h>
#include <string.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

const ttak_ntt_prime_t ttak_ntt_primes[TTAK_NTT_PRIME_COUNT] = {
    { 998244353ULL, 3ULL, 23U, 17450252288407896063ULL, 299560064ULL },
    { 1004535809ULL, 3ULL, 21U, 8214279848305098751ULL, 742115580ULL },
    { 469762049ULL, 3ULL, 26U, 18226067692438159359ULL, 118963808ULL }
};

/**
 * @brief Compute (a + b) mod mod without overflow.
 *
 * @param a   Left operand.
 * @param b   Right operand.
 * @param mod Prime modulus.
 * @return Modular sum.
 */
uint64_t ttak_mod_add(uint64_t a, uint64_t b, uint64_t mod) {
    uint64_t sum = a + b;
    if (sum >= mod || sum < a) {
        sum -= mod;
    }
    return sum;
}

/**
 * @brief Compute (a - b) mod mod, wrapping into the range [0, mod).
 *
 * @param a   Left operand.
 * @param b   Right operand.
 * @param mod Prime modulus.
 * @return Modular difference.
 */
uint64_t ttak_mod_sub(uint64_t a, uint64_t b, uint64_t mod) {
    return (a >= b) ? (a - b) : (a + mod - b);
}

/**
 * @brief Multiply two residues modulo mod using 128-bit intermediates.
 *
 * @param a   Left operand.
 * @param b   Right operand.
 * @param mod Prime modulus.
 * @return Modular product.
 */
uint64_t ttak_mod_mul(uint64_t a, uint64_t b, uint64_t mod) {
    ttak_uint128_native_t prod = (ttak_uint128_native_t)a * (ttak_uint128_native_t)b;
    return (uint64_t)(prod % mod);
}

/**
 * @brief Exponentiate a base modulo mod using square-and-multiply.
 *
 * @param base Base residue.
 * @param exp  Exponent.
 * @param mod  Prime modulus.
 * @return base^exp mod mod.
 */
uint64_t ttak_mod_pow(uint64_t base, uint64_t exp, uint64_t mod) {
    uint64_t result = 1 % mod;
    uint64_t factor = base % mod;
    while (exp) {
        if (exp & 1ULL) {
            result = ttak_mod_mul(result, factor, mod);
        }
        factor = ttak_mod_mul(factor, factor, mod);
        exp >>= 1ULL;
    }
    return result;
}

/**
 * @brief Compute the modular inverse using the extended Euclidean algorithm.
 *
 * @param value Input residue.
 * @param mod   Modulus.
 * @return Multiplicative inverse or 0 if none exists.
 */
uint64_t ttak_mod_inverse(uint64_t value, uint64_t mod) {
    int64_t t = 0;
    int64_t new_t = 1;
    int64_t r = (int64_t)mod;
    int64_t new_r = (int64_t)(value % mod);

    while (new_r != 0) {
        int64_t q = r / new_r;
        int64_t tmp_t = t - q * new_t;
        t = new_t;
        new_t = tmp_t;

        int64_t tmp_r = r - q * new_r;
        r = new_r;
        new_r = tmp_r;
    }

    if (r > 1) return 0;
    if (t < 0) t += (int64_t)mod;
    return (uint64_t)t;
}

/**
 * @brief Reduce a 128-bit product using Montgomery arithmetic.
 *
 * @param value Intermediate product.
 * @param prime Prime parameters (including Montgomery constants).
 * @return Reduced residue.
 */
uint64_t ttak_montgomery_reduce(ttak_uint128_native_t value, const ttak_ntt_prime_t *prime) {
    uint64_t m = (uint64_t)value * prime->montgomery_inv;
    ttak_uint128_native_t t = (value + (ttak_uint128_native_t)m * prime->modulus) >> 64;
    uint64_t result = (uint64_t)t;
    if (result >= prime->modulus) {
        result -= prime->modulus;
    }
    return result;
}

/**
 * @brief Multiply two residues in Montgomery space.
 *
 * @param lhs   Left operand (Montgomery form).
 * @param rhs   Right operand (Montgomery form).
 * @param prime Prime definition.
 * @return Product in Montgomery form.
 */
uint64_t ttak_montgomery_mul(uint64_t lhs, uint64_t rhs, const ttak_ntt_prime_t *prime) {
    ttak_uint128_native_t value = (ttak_uint128_native_t)lhs * (ttak_uint128_native_t)rhs;
    return ttak_montgomery_reduce(value, prime);
}

/**
 * @brief Convert a standard residue into Montgomery representation.
 *
 * @param value Value in standard form.
 * @param prime Prime definition with R^2 precomputed.
 * @return Value represented in Montgomery space.
 */
uint64_t ttak_montgomery_convert(uint64_t value, const ttak_ntt_prime_t *prime) {
    uint64_t v = value % prime->modulus;
    ttak_uint128_native_t converted = (ttak_uint128_native_t)v * prime->montgomery_r2;
    return ttak_montgomery_reduce(converted, prime);
}

/**
 * @brief Reorder the array into bit-reversed order.
 *
 * @param data Data array to permute.
 * @param n    Length of the array (power of two).
 */
static void bit_reverse(uint64_t *data, size_t n) {
    size_t j = 0;
    for (size_t i = 1; i < n; ++i) {
        size_t bit = n >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            uint64_t tmp = data[i];
            data[i] = data[j];
            data[j] = tmp;
        }
    }
}

/**
 * @brief Convert every element into Montgomery space.
 *
 * @param data  Array of residues.
 * @param n     Number of elements.
 * @param prime Prime modulus descriptor.
 */
static void montgomery_array_convert(uint64_t *data, size_t n, const ttak_ntt_prime_t *prime) {
    for (size_t i = 0; i < n; ++i) {
        data[i] = ttak_montgomery_convert(data[i], prime);
    }
}

/**
 * @brief Convert every element from Montgomery space back to standard form.
 *
 * @param data  Array of residues.
 * @param n     Number of elements.
 * @param prime Prime modulus descriptor.
 */
static void montgomery_array_restore(uint64_t *data, size_t n, const ttak_ntt_prime_t *prime) {
    for (size_t i = 0; i < n; ++i) {
        data[i] = ttak_montgomery_reduce((ttak_uint128_native_t)data[i], prime);
    }
}

/**
 * @brief Perform an in-place NTT or inverse NTT over the provided data.
 *
 * @param data   Array of residues to transform (length must be power of two).
 * @param n      Number of elements.
 * @param prime  Prime modulus descriptor.
 * @param inverse Set to true for inverse transform.
 * @return true on success, false if parameters are invalid.
 */
_Bool ttak_ntt_transform(uint64_t *data, size_t n, const ttak_ntt_prime_t *prime, _Bool inverse) {
    if (!data || !prime || n == 0) return false;
    if ((n & (n - 1)) != 0) return false;
    if (n > (size_t)(1ULL << prime->max_power_two)) return false;

    uint64_t modulus = prime->modulus;
    uint64_t unity = ttak_montgomery_convert(1ULL, prime);

    bit_reverse(data, n);
    montgomery_array_convert(data, n, prime);

    uint64_t root = ttak_mod_pow(prime->primitive_root, (prime->modulus - 1) / n, modulus);
    if (inverse) {
        root = ttak_mod_inverse(root, modulus);
    }

    for (size_t len = 1; len < n; len <<= 1) {
        uint64_t wlen = ttak_mod_pow(root, n / (len << 1), modulus);
        uint64_t wlen_mont = ttak_montgomery_convert(wlen, prime);

        for (size_t i = 0; i < n; i += (len << 1)) {
            uint64_t w = unity;
            for (size_t j = 0; j < len; ++j) {
                uint64_t u = data[i + j];
                uint64_t v = ttak_montgomery_mul(data[i + j + len], w, prime);
                uint64_t add = u + v;
                if (add >= modulus) add -= modulus;
                uint64_t sub = (u >= v) ? (u - v) : (u + modulus - v);
                data[i + j] = add;
                data[i + j + len] = sub;
                w = ttak_montgomery_mul(w, wlen_mont, prime);
            }
        }
    }

    if (inverse) {
        uint64_t inv_n = ttak_mod_inverse((uint64_t)n % modulus, modulus);
        uint64_t inv_n_mont = ttak_montgomery_convert(inv_n, prime);
        for (size_t i = 0; i < n; ++i) {
            data[i] = ttak_montgomery_mul(data[i], inv_n_mont, prime);
        }
    }

    montgomery_array_restore(data, n, prime);
    return true;
}

/**
 * @brief Multiply two transformed sequences element-wise.
 *
 * @param dst   Destination array.
 * @param lhs   Left operand.
 * @param rhs   Right operand.
 * @param n     Number of elements.
 * @param prime Prime modulus descriptor.
 */
void ttak_ntt_pointwise_mul(uint64_t *dst, const uint64_t *lhs, const uint64_t *rhs, size_t n, const ttak_ntt_prime_t *prime) {
    uint64_t mod = prime->modulus;
    for (size_t i = 0; i < n; ++i) {
        dst[i] = ttak_mod_mul(lhs[i], rhs[i], mod);
    }
}

/**
 * @brief Square each element of a transformed sequence.
 *
 * @param dst   Destination array.
 * @param src   Source array.
 * @param n     Number of elements.
 * @param prime Prime modulus descriptor.
 */
void ttak_ntt_pointwise_square(uint64_t *dst, const uint64_t *src, size_t n, const ttak_ntt_prime_t *prime) {
    uint64_t mod = prime->modulus;
    for (size_t i = 0; i < n; ++i) {
        dst[i] = ttak_mod_mul(src[i], src[i], mod);
    }
}

/**
 * @brief Round up to the next power of two.
 *
 * @param value Input value.
 * @return Next power of two >= value.
 */
size_t ttak_next_power_of_two(size_t value) {
    if (value == 0) return 1;
    value--;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
#if UINTPTR_MAX > 0xFFFFFFFFu
    value |= value >> 32;
#endif
    return value + 1;
}

/**
 * @brief Convert a native 128-bit integer into the portable struct.
 *
 * @param value Native 128-bit value.
 * @return Decomposed hi/lo representation.
 */
static ttak_u128_t make_u128(ttak_uint128_native_t value) {
    ttak_u128_t out;
    out.lo = (uint64_t)value;
    out.hi = (uint64_t)(value >> 64);
    return out;
}

/**
 * @brief Compute value mod mod using native 128-bit arithmetic.
 *
 * @param value 128-bit value.
 * @param mod   64-bit modulus.
 * @return Reduced 64-bit residue.
 */
static uint64_t mod128_u64(ttak_uint128_native_t value, uint64_t mod) {
    return (uint64_t)(value % mod);
}

/**
 * @brief Combine residues via the Chinese Remainder Theorem.
 *
 * @param terms       Array of residue/modulus pairs.
 * @param count       Number of terms.
 * @param residue_out Resulting residue in mixed-radix form.
 * @param modulus_out Combined modulus.
 * @return true on success, false if inversion fails.
 */
_Bool ttak_crt_combine(const ttak_crt_term_t *terms, size_t count, ttak_u128_t *residue_out, ttak_u128_t *modulus_out) {
    if (!terms || count == 0 || !residue_out || !modulus_out) return false;

    ttak_uint128_native_t result = terms[0].residue % terms[0].modulus;
    ttak_uint128_native_t modulus = terms[0].modulus;

    for (size_t i = 1; i < count; ++i) {
        uint64_t mod_i = terms[i].modulus;
        uint64_t residue_i = terms[i].residue % mod_i;
        uint64_t current_mod = mod128_u64(modulus, mod_i);
        uint64_t inverse = ttak_mod_inverse(current_mod, mod_i);
        if (inverse == 0) return false;

        uint64_t current_residue = mod128_u64(result, mod_i);
        uint64_t delta = ttak_mod_sub(residue_i, current_residue, mod_i);
        uint64_t k = ttak_mod_mul(delta, inverse, mod_i);
        result += (ttak_uint128_native_t)k * modulus;
        modulus *= mod_i;
    }

    *residue_out = make_u128(result);
    *modulus_out = make_u128(modulus);
    return true;
}
