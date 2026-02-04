#ifndef TTAK_TYPES_FIXED_H
#define TTAK_TYPES_FIXED_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t lo;
    uint64_t hi;
} ttak_u128_t;

typedef struct {
    uint64_t limb[4]; /* limb[0] = least-significant chunk */
} ttak_u256_t;

static inline ttak_u128_t ttak_u128_make(uint64_t hi, uint64_t lo) {
    ttak_u128_t out;
    out.lo = lo;
    out.hi = hi;
    return out;
}

static inline ttak_u128_t ttak_u128_from_u64(uint64_t value) {
    return ttak_u128_make(0, value);
}

static inline ttak_u128_t ttak_u128_zero(void) {
    return ttak_u128_make(0, 0);
}

static inline uint64_t ttak_u128_get_lo(ttak_u128_t v) { return v.lo; }
static inline uint64_t ttak_u128_get_hi(ttak_u128_t v) { return v.hi; }

static inline int ttak_u128_cmp(ttak_u128_t a, ttak_u128_t b) {
    if (a.hi < b.hi) return -1;
    if (a.hi > b.hi) return 1;
    if (a.lo < b.lo) return -1;
    if (a.lo > b.lo) return 1;
    return 0;
}

static inline ttak_u128_t ttak_u128_add(ttak_u128_t a, ttak_u128_t b) {
    ttak_u128_t out;
    out.lo = a.lo + b.lo;
    uint64_t carry = (out.lo < a.lo) ? 1 : 0;
    out.hi = a.hi + b.hi + carry;
    return out;
}

static inline bool ttak_u128_add_overflow(ttak_u128_t a, ttak_u128_t b, ttak_u128_t *out) {
    ttak_u128_t tmp = ttak_u128_add(a, b);
    bool of = (tmp.hi < a.hi) || (tmp.hi < b.hi);
    if (out) *out = tmp;
    return of;
}

static inline ttak_u128_t ttak_u128_add64(ttak_u128_t a, uint64_t b) {
    ttak_u128_t out;
    out.lo = a.lo + b;
    out.hi = a.hi + (out.lo < b ? 1 : 0);
    return out;
}

static inline ttak_u128_t ttak_u128_sub(ttak_u128_t a, ttak_u128_t b) {
    ttak_u128_t out;
    uint64_t borrow = (a.lo < b.lo) ? 1 : 0;
    out.lo = a.lo - b.lo;
    out.hi = a.hi - b.hi - borrow;
    return out;
}

static inline bool ttak_u128_sub_underflow(ttak_u128_t a, ttak_u128_t b, ttak_u128_t *out) {
    bool under = ttak_u128_cmp(a, b) < 0;
    if (!under && out) *out = ttak_u128_sub(a, b);
    return under;
}

static inline ttak_u128_t ttak_u128_sub64(ttak_u128_t a, uint64_t b) {
    ttak_u128_t out;
    uint64_t borrow = (a.lo < b) ? 1 : 0;
    out.lo = a.lo - b;
    out.hi = a.hi - borrow;
    return out;
}

static inline ttak_u128_t ttak_u128_and(ttak_u128_t a, ttak_u128_t b) {
    ttak_u128_t out;
    out.lo = a.lo & b.lo;
    out.hi = a.hi & b.hi;
    return out;
}

static inline ttak_u128_t ttak_u128_shl(ttak_u128_t v, unsigned shift) {
    if (shift >= 128) return ttak_u128_zero();
    if (shift == 0) return v;
    if (shift >= 64) {
        uint64_t hi = v.lo << (shift - 64);
        return ttak_u128_make(hi, 0);
    }
    uint64_t hi = (v.hi << shift) | (v.lo >> (64 - shift));
    uint64_t lo = v.lo << shift;
    return ttak_u128_make(hi, lo);
}

static inline ttak_u128_t ttak_u128_shr(ttak_u128_t v, unsigned shift) {
    if (shift >= 128) return ttak_u128_zero();
    if (shift == 0) return v;
    if (shift >= 64) {
        uint64_t lo = v.hi >> (shift - 64);
        return ttak_u128_make(0, lo);
    }
    uint64_t hi = v.hi >> shift;
    uint64_t lo = (v.lo >> shift) | (v.hi << (64 - shift));
    return ttak_u128_make(hi, lo);
}

static inline bool ttak_u128_is_zero(ttak_u128_t v) {
    return v.hi == 0 && v.lo == 0;
}

static inline uint64_t ttak_u128_bit(ttak_u128_t v, unsigned bit) {
    if (bit >= 128) return 0;
    if (bit >= 64) {
        return (v.hi >> (bit - 64)) & 1ULL;
    }
    return (v.lo >> bit) & 1ULL;
}

static inline void ttak_mul_64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
    /* Inline ISA primitives keep TinyCC builds fast without optimizer help. */
#if defined(__x86_64__)
    uint64_t lo_tmp, hi_tmp;
    __asm__ __volatile__("mulq %[rhs]"
                         : "=a"(lo_tmp), "=d"(hi_tmp)
                         : "0"(a), [rhs] "r"(b)
                         : "cc");
    *lo = lo_tmp;
    *hi = hi_tmp;
    return;
#elif defined(__aarch64__)
    uint64_t lo_tmp, hi_tmp;
    __asm__ __volatile__(
        "mul %0, %2, %3\n"
        "umulh %1, %2, %3"
        : "=&r"(lo_tmp), "=&r"(hi_tmp)
        : "r"(a), "r"(b));
    *lo = lo_tmp;
    *hi = hi_tmp;
    return;
#elif defined(__riscv_xlen) && (__riscv_xlen == 64)
    uint64_t lo_tmp, hi_tmp;
    __asm__ __volatile__(
        "mul %0, %2, %3\n"
        "mulhu %1, %2, %3"
        : "=&r"(lo_tmp), "=&r"(hi_tmp)
        : "r"(a), "r"(b));
    *lo = lo_tmp;
    *hi = hi_tmp;
    return;
#endif

    uint64_t a_lo = (uint32_t)a;
    uint64_t a_hi = a >> 32;
    uint64_t b_lo = (uint32_t)b;
    uint64_t b_hi = b >> 32;

    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;

    uint64_t mid = (p0 >> 32) + (p1 & 0xFFFFFFFFULL) + (p2 & 0xFFFFFFFFULL);
    uint64_t mid_hi = (mid >> 32) + (p1 >> 32) + (p2 >> 32);

    *lo = (mid << 32) | (p0 & 0xFFFFFFFFULL);
    *hi = p3 + mid_hi;
}

static inline ttak_u128_t ttak_u128_mul_u64_scalar(uint64_t a, uint64_t b) {
    ttak_u128_t out;
    ttak_mul_64(a, b, &out.hi, &out.lo);
    return out;
}

static inline uint64_t ttak_u64_mul_lo(uint64_t a, uint64_t b) {
    uint64_t hi, lo;
    ttak_mul_64(a, b, &hi, &lo);
    (void)hi;
    return lo;
}

static inline ttak_u128_t ttak_u128_mul_u64_wide(ttak_u128_t value, uint64_t factor, bool *overflow) {
    ttak_u128_t part_lo = ttak_u128_mul_u64_scalar(value.lo, factor);
    ttak_u128_t out = ttak_u128_make(0, part_lo.lo);
    uint64_t carry = part_lo.hi;

    uint64_t hi_high, hi_low;
    ttak_mul_64(value.hi, factor, &hi_high, &hi_low);

    out.hi = hi_low + carry;
    bool of = (out.hi < carry) || hi_high != 0;
    if (overflow) *overflow = of;
    return out;
}

static inline ttak_u128_t ttak_u128_mul64(uint64_t a, uint64_t b) {
    return ttak_u128_mul_u64_scalar(a, b);
}

static inline uint64_t ttak_u128_mod_u64(ttak_u128_t value, uint64_t mod) {
    if (mod == 0) return 0;
    uint64_t rem = 0;
    for (int bit = 127; bit >= 0; --bit) {
        rem = (rem << 1) | ttak_u128_bit(value, (unsigned)bit);
        if (rem >= mod) rem -= mod;
    }
    return rem;
}

static inline ttak_u256_t ttak_u256_zero(void) {
    ttak_u256_t out = {{0, 0, 0, 0}};
    return out;
}

static inline ttak_u256_t ttak_u256_from_limbs(uint64_t l3, uint64_t l2, uint64_t l1, uint64_t l0) {
    ttak_u256_t out = {{l0, l1, l2, l3}};
    return out;
}

static inline ttak_u256_t ttak_u256_from_u128(ttak_u128_t value) {
    return ttak_u256_from_limbs(0, 0, value.hi, value.lo);
}

static inline ttak_u128_t ttak_u256_low128(ttak_u256_t value) {
    return ttak_u128_make(value.limb[1], value.limb[0]);
}

static inline ttak_u128_t ttak_u256_high128(ttak_u256_t value) {
    return ttak_u128_make(value.limb[3], value.limb[2]);
}

static inline ttak_u256_t ttak_u256_add(ttak_u256_t a, ttak_u256_t b) {
    ttak_u256_t out;
    uint64_t carry = 0;
    for (int i = 0; i < 4; ++i) {
        uint64_t sum = a.limb[i] + b.limb[i];
        uint64_t new_sum = sum + carry;
        uint64_t new_carry = (sum < a.limb[i]) || (new_sum < sum);
        out.limb[i] = new_sum;
        carry = new_carry;
    }
    return out;
}

static inline ttak_u256_t ttak_u256_shr(ttak_u256_t v, unsigned shift) {
    if (shift >= 256) return ttak_u256_zero();
    if (shift == 0) return v;
    ttak_u256_t out = ttak_u256_zero();
    unsigned limb_shift = shift / 64;
    unsigned bit_shift = shift % 64;
    for (int i = 3; i >= 0; --i) {
        if ((unsigned)i < limb_shift) continue;
        uint64_t chunk = v.limb[i];
        unsigned target = i - limb_shift;
        out.limb[target] |= chunk >> bit_shift;
        if (bit_shift && target > 0) {
            out.limb[target - 1] |= chunk << (64 - bit_shift);
        }
    }
    return out;
}

static inline ttak_u256_t ttak_u256_shl(ttak_u256_t v, unsigned shift) {
    if (shift >= 256) return ttak_u256_zero();
    if (shift == 0) return v;
    ttak_u256_t out = ttak_u256_zero();
    unsigned limb_shift = shift / 64;
    unsigned bit_shift = shift % 64;
    for (int i = 0; i < 4; ++i) {
        if (i + limb_shift >= 4) break;
        uint64_t chunk = v.limb[i];
        unsigned target = i + limb_shift;
        out.limb[target] |= chunk << bit_shift;
        if (bit_shift && target + 1 < 4) {
            out.limb[target + 1] |= chunk >> (64 - bit_shift);
        }
    }
    return out;
}

static inline ttak_u256_t ttak_u128_mul_u128(ttak_u128_t a, ttak_u128_t b) {
    uint64_t lo_hi, lo_lo;
    ttak_mul_64(a.lo, b.lo, &lo_hi, &lo_lo);

    uint64_t cross1_hi, cross1_lo;
    ttak_mul_64(a.lo, b.hi, &cross1_hi, &cross1_lo);

    uint64_t cross2_hi, cross2_lo;
    ttak_mul_64(a.hi, b.lo, &cross2_hi, &cross2_lo);

    uint64_t hi_hi, hi_lo;
    ttak_mul_64(a.hi, b.hi, &hi_hi, &hi_lo);

    uint64_t limb0 = lo_lo;

    uint64_t limb1 = lo_hi;
    uint64_t carry = 0;
    uint64_t sum = limb1 + cross1_lo;
    carry = (sum < limb1) ? 1 : 0;
    limb1 = sum;
    sum = limb1 + cross2_lo;
    carry += (sum < limb1) ? 1 : 0;
    limb1 = sum;

    uint64_t limb2 = cross1_hi;
    uint64_t carry2 = 0;
    sum = limb2 + cross2_hi;
    carry2 = (sum < limb2) ? 1 : 0;
    limb2 = sum;
    sum = limb2 + hi_lo;
    carry2 += (sum < limb2) ? 1 : 0;
    limb2 = sum;
    limb2 += carry;
    if (limb2 < carry) carry2++;

    uint64_t limb3 = hi_hi + carry2;

    return ttak_u256_from_limbs(limb3, limb2, limb1, limb0);
}

static inline ttak_u128_t ttak_u256_extract_low(ttak_u256_t value) {
    return ttak_u256_low128(value);
}

static inline ttak_u128_t ttak_u256_extract_high(ttak_u256_t value) {
    return ttak_u256_high128(value);
}

#endif /* TTAK_TYPES_FIXED_H */
