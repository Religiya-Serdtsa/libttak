#include <ttak/math/bigreal.h>
#include <ttak/mem/mem.h>
#include <string.h>

void ttak_bigreal_init(ttak_bigreal_t *br, uint64_t now) {
    ttak_bigint_init(&br->mantissa, now);
    br->exponent = 0;
}

void ttak_bigreal_init_u64(ttak_bigreal_t *br, uint64_t value, uint64_t now) {
    ttak_bigint_init_u64(&br->mantissa, value, now);
    br->exponent = 0;
}

void ttak_bigreal_free(ttak_bigreal_t *br, uint64_t now) {
    ttak_bigint_free(&br->mantissa, now);
}

_Bool ttak_bigreal_copy(ttak_bigreal_t *dst, const ttak_bigreal_t *src, uint64_t now) {
    dst->exponent = src->exponent;
    return ttak_bigint_copy(&dst->mantissa, &src->mantissa, now);
}

_Bool ttak_bigreal_align(ttak_bigreal_t *a, ttak_bigreal_t *b, uint64_t now) {
    if (a->exponent == b->exponent) return true;
    
    ttak_bigint_t tmp;
    ttak_bigint_init(&tmp, now);

    if (a->exponent > b->exponent) {
        // Shift a left
        uint64_t diff = a->exponent - b->exponent;
        if (diff > 60) {
            ttak_bigint_free(&tmp, now);
            return false;
        }
        ttak_bigint_mul_u64(&tmp, &a->mantissa, 1ULL << diff, now);
        ttak_bigint_copy(&a->mantissa, &tmp, now);
        a->exponent = b->exponent;
    } else {
        // Shift b left
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
    _Bool ok = ttak_bigint_add(&dst->mantissa, &l.mantissa, &r.mantissa, now);
    
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
    _Bool ok = ttak_bigint_sub(&dst->mantissa, &l.mantissa, &r.mantissa, now);
    
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
