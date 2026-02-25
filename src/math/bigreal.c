#include <ttak/math/bigreal.h>
#include <ttak/mem/mem.h>
#include <string.h>
#include <stdalign.h>

/**
 * Cheonwonsul(Jungha, Hong et al.)
 * Logic: Aligned limb processing to map to cache lines.
 */
static _Bool ttak_bigreal_op_cheonwonsul(ttak_bigreal_t *dst, const ttak_bigreal_t *lhs, const ttak_bigreal_t *rhs, _Bool is_sub, uint64_t now) {
    if (is_sub) {
        return ttak_bigint_sub(&dst->mantissa, &lhs->mantissa, &rhs->mantissa, now);
    } else {
        return ttak_bigint_add(&dst->mantissa, &lhs->mantissa, &rhs->mantissa, now);
    }
}


static _Bool ttak_bigreal_export_u128(const ttak_bigreal_t *src, ttak_u128_t *out) {
    if (!src || !out) return false;
    if (src->mantissa.is_negative || src->exponent != 0) return false;
    return ttak_bigint_export_u128(&src->mantissa, out);
}

static _Bool ttak_bigreal_set_u128(ttak_bigreal_t *dst, ttak_u128_t value, uint64_t tt_now) {
    if (!dst) return false;
    dst->exponent = 0;
    return ttak_bigint_set_u128(&dst->mantissa, value, tt_now);
}

static ttak_u128_t ttak_bigreal_al_kashi_seed64(ttak_u128_t src_u128) {
    uint64_t hi = ttak_u128_get_hi(src_u128);
    uint64_t lo = ttak_u128_get_lo(src_u128);
    if (hi == 0 && lo < 2) {
        return src_u128;
    }

    unsigned bitlen = (hi != 0)
        ? (64u + (64u - (unsigned)__builtin_clzll(hi)))
        : (64u - (unsigned)__builtin_clzll(lo));

    unsigned root_bits = (bitlen + 1u) >> 1;
    unsigned shift = (root_bits > 1u) ? (root_bits - 1u) : 0u;

    return ttak_u128_shl(ttak_u128_from_u64(1), shift);
}

/**
 * @brief Base-64 Al-Kashi refinement kernel for unsigned u128 square roots.
 *
 * Iteration:
 *   x_{n+1} = ((63 * x_n) + (n / x_n)) >> 6
 *
 * The division by 64 is implemented with a right-shift (`>> 6`) for
 * cycle-level efficiency while preserving deterministic monotonic refinement.
 */
static _Bool ttak_bigreal_sqrt_u128_base64(ttak_u128_t *root_out, ttak_u128_t src_u128, uint64_t tt_now) {
    if (!root_out) return false;
    if (ttak_u128_is_zero(src_u128)) {
        *root_out = ttak_u128_zero();
        return true;
    }

    ttak_bigreal_t n_br, x_br, q_br, weighted_br, sum_br, x_next_br;
    ttak_bigreal_init(&n_br, tt_now);
    ttak_bigreal_init(&x_br, tt_now);
    ttak_bigreal_init(&q_br, tt_now);
    ttak_bigreal_init(&weighted_br, tt_now);
    ttak_bigreal_init(&sum_br, tt_now);
    ttak_bigreal_init(&x_next_br, tt_now);

    _Bool ok = true;
    ttak_u128_t x_u128 = ttak_bigreal_al_kashi_seed64(src_u128);

    if (!ttak_bigreal_set_u128(&n_br, src_u128, tt_now)) ok = false;
    if (ok && !ttak_bigreal_set_u128(&x_br, x_u128, tt_now)) ok = false;

    for (uint8_t iter = 0; ok && iter < 24; ++iter) {
        if (!ttak_bigreal_div(&q_br, &n_br, &x_br, tt_now)) {
            ok = false;
            break;
        }

        if (!ttak_bigint_mul_u64(&weighted_br.mantissa, &x_br.mantissa, 63u, tt_now)) {
            ok = false;
            break;
        }
        weighted_br.exponent = x_br.exponent;
        weighted_br.mantissa.is_negative = false;

        if (!ttak_bigreal_add(&sum_br, &weighted_br, &q_br, tt_now)) {
            ok = false;
            break;
        }

        ttak_u128_t sum_u128;
        if (!ttak_bigreal_export_u128(&sum_br, &sum_u128)) {
            ok = false;
            break;
        }

        ttak_u128_t x_next = ttak_u128_shr(sum_u128, 6);
        if (ttak_u128_is_zero(x_next)) {
            x_next = ttak_u128_from_u64(1);
        }

        if (ttak_u128_cmp(x_next, x_u128) == 0) {
            x_u128 = x_next;
            break;
        }

        x_u128 = x_next;
        if (!ttak_bigreal_set_u128(&x_next_br, x_u128, tt_now)) {
            ok = false;
            break;
        }
        if (!ttak_bigreal_copy(&x_br, &x_next_br, tt_now)) {
            ok = false;
            break;
        }
    }

    if (ok) {
        *root_out = x_u128;
    }

    ttak_bigreal_free(&n_br, tt_now);
    ttak_bigreal_free(&x_br, tt_now);
    ttak_bigreal_free(&q_br, tt_now);
    ttak_bigreal_free(&weighted_br, tt_now);
    ttak_bigreal_free(&sum_br, tt_now);
    ttak_bigreal_free(&x_next_br, tt_now);
    return ok;
}

void ttak_bigreal_init(ttak_bigreal_t *br, uint64_t now) {
    ttak_bigint_init(&br->mantissa, now);
    br->exponent = 0;
}

void ttak_bigreal_init_u64(ttak_bigreal_t *br, uint64_t value, uint64_t now) {
    ttak_bigint_init_u64(&br->mantissa, value, now);
    br->exponent = 0;
}

/**
 * @brief Release the mantissa storage inside the big real.
 *
 * @param br  Big real to destroy.
 * @param now Timestamp forwarded to bigint free.
 */
void ttak_bigreal_free(ttak_bigreal_t *br, uint64_t now) {
    ttak_bigint_free(&br->mantissa, now);
}

_Bool ttak_bigreal_copy(ttak_bigreal_t *dst, const ttak_bigreal_t *src, uint64_t now) {
    if (dst == src) return true;
    dst->exponent = src->exponent;
    return ttak_bigint_copy(&dst->mantissa, &src->mantissa, now);
}

_Bool ttak_bigreal_align(ttak_bigreal_t *a, ttak_bigreal_t *b, uint64_t now) {
    if (a->exponent == b->exponent) return true;
    
    ttak_bigint_t tmp;
    ttak_bigint_init(&tmp, now);

    if (a->exponent > b->exponent) {
        // Shift a left to match smaller exponent b
        uint64_t diff = a->exponent - b->exponent;
        if (diff > 60) {
            ttak_bigint_free(&tmp, now);
            return false;
        }
        ttak_bigint_mul_u64(&tmp, &a->mantissa, 1ULL << diff, now);
        ttak_bigint_copy(&a->mantissa, &tmp, now);
        a->exponent = b->exponent;
    } else {
        // Shift b left to match smaller exponent a
        uint64_t diff = b->exponent - a->exponent;
        if (diff > 60) {
            ttak_bigint_free(&tmp, now);
            return false;
        }
        ttak_bigint_mul_u64(&tmp, &b->mantissa, 1ULL << diff, now);
        ttak_bigint_copy(&b->mantissa, &tmp, now);
        b->exponent = a->exponent;
    }
    ttak_bigint_free(&tmp, now);
    return true;
}

/**
 * @brief Add two big reals with matching exponents.
 *
 * @param dst Destination big real.
 * @param lhs Left operand.
 * @param rhs Right operand.
 * @param now Timestamp for bigint arithmetic.
 * @return true on success, false if exponents differ.
 */
_Bool ttak_bigreal_add(ttak_bigreal_t *dst, const ttak_bigreal_t *lhs, const ttak_bigreal_t *rhs, uint64_t now) {
    ttak_bigreal_t l, r;
    ttak_bigreal_init(&l, now);
    ttak_bigreal_init(&r, now);
    ttak_bigreal_copy(&l, lhs, now);
    ttak_bigreal_copy(&r, rhs, now);
    
    if (!ttak_bigreal_align(&l, &r, now)) {
        ttak_bigreal_free(&l, now);
        ttak_bigreal_free(&r, now);
        return false;
    }
    
    dst->exponent = l.exponent;
    /* Cheonwonsul: Aligned internal limb processing */
    _Bool ok = ttak_bigreal_op_cheonwonsul(dst, &l, &r, false, now);
    
    ttak_bigreal_free(&l, now);
    ttak_bigreal_free(&r, now);
    return ok;
}

_Bool ttak_bigreal_sub(ttak_bigreal_t *dst, const ttak_bigreal_t *lhs, const ttak_bigreal_t *rhs, uint64_t now) {
    ttak_bigreal_t l, r;
    ttak_bigreal_init(&l, now);
    ttak_bigreal_init(&r, now);
    ttak_bigreal_copy(&l, lhs, now);
    ttak_bigreal_copy(&r, rhs, now);
    
    if (!ttak_bigreal_align(&l, &r, now)) {
        ttak_bigreal_free(&l, now);
        ttak_bigreal_free(&r, now);
        return false;
    }
    
    dst->exponent = l.exponent;
    /* Cheonwonsul: Aligned internal limb processing */
    _Bool ok = ttak_bigreal_op_cheonwonsul(dst, &l, &r, true, now);
    
    ttak_bigreal_free(&l, now);
    ttak_bigreal_free(&r, now);
    return ok;
}

_Bool ttak_bigreal_mul(ttak_bigreal_t *dst, const ttak_bigreal_t *lhs, const ttak_bigreal_t *rhs, uint64_t now) {
    dst->exponent = lhs->exponent + rhs->exponent;
    return ttak_bigint_mul(&dst->mantissa, &lhs->mantissa, &rhs->mantissa, now);
}

_Bool ttak_bigreal_div(ttak_bigreal_t *dst, const ttak_bigreal_t *lhs, const ttak_bigreal_t *rhs, uint64_t now) {
    // Basic division: q = (l.m / r.m) * 10^(l.e - r.e)
    // We might want more precision by shifting l.m left first.
    ttak_bigint_t r_rem;
    ttak_bigint_init(&r_rem, now);
    
    dst->exponent = lhs->exponent - rhs->exponent;
    _Bool ok = ttak_bigint_div(&dst->mantissa, &r_rem, &lhs->mantissa, &rhs->mantissa, now);
    
    ttak_bigint_free(&r_rem, now);
    return ok;
}

int ttak_bigreal_cmp(const ttak_bigreal_t *lhs, const ttak_bigreal_t *rhs, uint64_t now) {
    ttak_bigreal_t l, r;
    ttak_bigreal_init(&l, now);
    ttak_bigreal_init(&r, now);
    ttak_bigreal_copy(&l, lhs, now);
    ttak_bigreal_copy(&r, rhs, now);
    
    if (!ttak_bigreal_align(&l, &r, now)) {
        // Fallback or error?
        ttak_bigreal_free(&l, now);
        ttak_bigreal_free(&r, now);
        return 0; 
    }
    
    int res = ttak_bigint_cmp(&l.mantissa, &r.mantissa);
    ttak_bigreal_free(&l, now);
    ttak_bigreal_free(&r, now);
    return res;
}


_Bool ttak_bigreal_sqrt(ttak_bigreal_t *res, const ttak_bigreal_t *src, uint64_t tt_now) {
    if (!res || !src) return false;
    if (src->mantissa.is_negative) return false;

    ttak_u128_t src_u128;
    if (!ttak_bigreal_export_u128(src, &src_u128)) {
        return false;
    }

    ttak_u128_t root_u128;
    if (!ttak_bigreal_sqrt_u128_base64(&root_u128, src_u128, tt_now)) {
        return false;
    }

    return ttak_bigreal_set_u128(res, root_u128, tt_now);
}
