#ifndef TTAK_HT_WYHASH_H
#define TTAK_HT_WYHASH_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/**
 * @brief wyhash 64-bit implementation (v4).
 * 
 * Optimized for speed and quality, using bit manipulation and branchless
 * logic where possible.
 */

static inline uint64_t _ttak_wymum(uint64_t A, uint64_t B) {
#ifdef __SIZEOF_INT128__
    __uint128_t r = (__uint128_t)A * B;
    return (uint64_t)r ^ (uint64_t)(r >> 64);
#else
    uint64_t ha = A >> 32, hb = B >> 32, la = (uint32_t)A, lb = (uint32_t)B;
    uint64_t rh = ha * hb, rm0 = ha * lb, rm1 = hb * la, rl = la * lb;
    uint64_t t = rl + (rm0 << 32), c = t < rl;
    uint64_t lo = t + (rm1 << 32), hi = rh + (rm0 >> 32) + (rm1 >> 32) + c + (lo < t);
    return hi ^ lo;
#endif
}

static inline uint64_t _ttak_wyr8(const uint8_t *p) {
    uint64_t v; memcpy(&v, p, 8); return v;
}
static inline uint64_t _ttak_wyr4(const uint8_t *p) {
    uint32_t v; memcpy(&v, p, 4); return v;
}
static inline uint64_t _ttak_wyr3(const uint8_t *p, size_t k) {
    return (((uint64_t)p[0]) << 16) | (((uint64_t)p[k >> 1]) << 8) | p[k - 1];
}

static inline uint64_t ttak_wyhash(const void *key, size_t len, uint64_t seed) {
    const uint8_t *p = (const uint8_t *)key;
    uint64_t a, b;
    seed ^= 0xa0761d6478bd642fULL;
    if (len <= 16) {
        if (len >= 4) {
            a = (_ttak_wyr4(p) << 32) | _ttak_wyr4(p + len - 4);
            b = (_ttak_wyr4(p + ((len >> 3) << 2)) << 32) | _ttak_wyr4(p + len - 4 - ((len >> 3) << 2));
        } else if (len > 0) {
            a = _ttak_wyr3(p, len);
            b = 0;
        } else a = b = 0;
    } else {
        size_t i = len;
        if (i > 48) {
            uint64_t see1 = seed, see2 = seed;
            do {
                seed = _ttak_wymum(_ttak_wyr8(p) ^ 0xe7037ed1a0b428dbULL, _ttak_wyr8(p + 8) ^ seed);
                see1 = _ttak_wymum(_ttak_wyr8(p + 16) ^ 0x8ebc6af09c88c6e3ULL, _ttak_wyr8(p + 24) ^ see1);
                see2 = _ttak_wymum(_ttak_wyr8(p + 32) ^ 0x589965cc75374cc3ULL, _ttak_wyr8(p + 40) ^ see2);
                p += 48; i -= 48;
            } while (i > 48);
            seed ^= see1 ^ see2;
        }
        while (i > 16) {
            seed = _ttak_wymum(_ttak_wyr8(p) ^ 0xe7037ed1a0b428dbULL, _ttak_wyr8(p + 8) ^ seed);
            p += 16; i -= 16;
        }
        a = _ttak_wyr8(p + i - 16);
        b = _ttak_wyr8(p + i - 8);
    }
    return _ttak_wymum(a ^ 0xe7037ed1a0b428dbULL, b ^ seed ^ len);
}

/**
 * @brief Fast hashing of a 64-bit integer using wyhash principles.
 */
static inline uint64_t ttak_hash_u64(uint64_t val, uint64_t seed) {
    return _ttak_wymum(val ^ 0xa0761d6478bd642fULL, seed ^ 0xe7037ed1a0b428dbULL);
}

/**
 * @brief Fast range mapping (FastMod) to avoid modulo operation.
 * 
 * Maps a 64-bit hash to [0, range) using a single 128-bit multiplication.
 */
static inline uint64_t ttak_fast_range(uint64_t hash, uint64_t range) {
#ifdef __SIZEOF_INT128__
    __uint128_t m = (__uint128_t)hash * range;
    return (uint64_t)(m >> 64);
#else
    return hash % range;
#endif
}

#endif // TTAK_HT_WYHASH_H
