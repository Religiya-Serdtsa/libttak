#include <ttak/math/vector.h>
#include <ttak/mem/mem.h>
#include <ttak/mem/owner.h>
#include <ttak/shared/shared.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static void vector_payload_cleanup(void *data) {
    if (!data) return;
    ttak_vector_t *v_ptr = (ttak_vector_t *)data;
    for (int i = 0; i < 4; i++) {
        ttak_bigreal_free(&v_ptr->elements[i], 0);
    }
}

tt_shared_vector_t* ttak_vector_create(uint8_t dim, tt_owner_t *owner, uint64_t tt_now) {
    if (dim < 2 || dim > 4) return NULL;

    tt_shared_vector_t *sv = (tt_shared_vector_t *)malloc(sizeof(tt_shared_vector_t));
    if (!sv) return NULL;

    ttak_shared_init(&sv->base);
    sv->base.cleanup = vector_payload_cleanup;

    ttak_shared_result_t res = sv->base.allocate_typed(&sv->base, sizeof(ttak_vector_t), "ttak_vector_t", TTAK_SHARED_LEVEL_3);
    if (res != TTAK_OWNER_SUCCESS) {
        free(sv);
        return NULL;
    }

    ttak_shared_result_t a_res = sv->base.add_owner(&sv->base, owner);
    if (a_res != TTAK_OWNER_SUCCESS) {
        ttak_shared_destroy(&sv->base);
        free(sv);
        return NULL;
    }

    ttak_vector_t *v_ptr = (ttak_vector_t *)sv->base.access(&sv->base, owner, &a_res);
    if (!v_ptr) {
        ttak_shared_destroy(&sv->base);
        free(sv);
        return NULL;
    }

    v_ptr->dim = dim;
    for (int i = 0; i < 4; i++) {
        ttak_bigreal_init(&v_ptr->elements[i], tt_now);
    }
    sv->base.release(&sv->base);

    return sv;
}

void ttak_vector_destroy(tt_shared_vector_t *sv, uint64_t tt_now) {
    if (!sv) return;
    (void)tt_now;
    ttak_shared_destroy(&sv->base);
    free(sv);
}

ttak_bigreal_t* ttak_vector_get(tt_shared_vector_t *sv, tt_owner_t *owner, uint8_t index, uint64_t tt_now) {
    if (!sv || !owner) return NULL;
    (void)tt_now;

    ttak_shared_result_t res;
    ttak_vector_t *v_ptr = (ttak_vector_t *)sv->base.access(&sv->base, owner, &res);
    if (!v_ptr) return NULL;

    if (index >= v_ptr->dim) {
        sv->base.release(&sv->base);
        return NULL;
    }

    return &v_ptr->elements[index];
}

bool ttak_vector_set(tt_shared_vector_t *sv, tt_owner_t *owner, uint8_t index, const ttak_bigreal_t *val, uint64_t tt_now) {
    if (!sv || !owner || !val) return false;

    ttak_shared_result_t res;
    ttak_vector_t *v_ptr = (ttak_vector_t *)sv->base.access(&sv->base, owner, &res);
    if (!v_ptr) return false;

    if (index >= v_ptr->dim) {
        sv->base.release(&sv->base);
        return false;
    }

    bool ok = ttak_bigreal_copy(&v_ptr->elements[index], val, tt_now);
    sv->base.release(&sv->base);
    return ok;
}

bool ttak_vector_dot(ttak_bigreal_t *res, tt_shared_vector_t *a, tt_shared_vector_t *b, tt_owner_t *owner, uint64_t tt_now) {
    if (!res || !a || !b || !owner) return false;

    ttak_shared_result_t res_a, res_b;
    ttak_vector_t *va = (ttak_vector_t *)a->base.access(&a->base, owner, &res_a);
    ttak_vector_t *vb = (ttak_vector_t *)b->base.access(&b->base, owner, &res_b);

    if (!va || !vb || va->dim != vb->dim) {
        if (va) a->base.release(&a->base);
        if (vb) b->base.release(&b->base);
        return false;
    }

    ttak_bigreal_t sum, prod;
    ttak_bigreal_init(&sum, tt_now);
    ttak_bigreal_init(&prod, tt_now);

    bool success = true;
    for (uint8_t i = 0; i < va->dim; i++) {
        if (!ttak_bigreal_mul(&prod, &va->elements[i], &vb->elements[i], tt_now)) {
            success = false;
            break;
        }
        if (!ttak_bigreal_add(&sum, &sum, &prod, tt_now)) {
            success = false;
            break;
        }
    }

    if (success) {
        ttak_bigreal_copy(res, &sum, tt_now);
    }

    ttak_bigreal_free(&sum, tt_now);
    ttak_bigreal_free(&prod, tt_now);

    a->base.release(&a->base);
    b->base.release(&b->base);

    return success;
}

bool ttak_vector_cross(tt_shared_vector_t *res, tt_shared_vector_t *a, tt_shared_vector_t *b, tt_owner_t *owner, uint64_t tt_now) {
    ttak_shared_result_t ra, rb, rr;
    ttak_vector_t *va = (ttak_vector_t *)a->base.access(&a->base, owner, &ra);
    ttak_vector_t *vb = (ttak_vector_t *)b->base.access(&b->base, owner, &rb);
    ttak_vector_t *vr = (ttak_vector_t *)res->base.access(&res->base, owner, &rr);

    if (!va || !vb || !vr || va->dim != 3 || vb->dim != 3 || vr->dim != 3) {
        if (va) a->base.release(&a->base);
        if (vb) b->base.release(&b->base);
        if (vr) res->base.release(&res->base);
        return false;
    }

    ttak_bigreal_t t1, t2;
    ttak_bigreal_init(&t1, tt_now);
    ttak_bigreal_init(&t2, tt_now);

    ttak_bigreal_mul(&t1, &va->elements[1], &vb->elements[2], tt_now);
    ttak_bigreal_mul(&t2, &va->elements[2], &vb->elements[1], tt_now);
    ttak_bigreal_sub(&vr->elements[0], &t1, &t2, tt_now);

    ttak_bigreal_mul(&t1, &va->elements[2], &vb->elements[0], tt_now);
    ttak_bigreal_mul(&t2, &va->elements[0], &vb->elements[2], tt_now);
    ttak_bigreal_sub(&vr->elements[1], &t1, &t2, tt_now);

    ttak_bigreal_mul(&t1, &va->elements[0], &vb->elements[1], tt_now);
    ttak_bigreal_mul(&t2, &va->elements[1], &vb->elements[0], tt_now);
    ttak_bigreal_sub(&vr->elements[2], &t1, &t2, tt_now);

    ttak_bigreal_free(&t1, tt_now);
    ttak_bigreal_free(&t2, tt_now);

    a->base.release(&a->base);
    b->base.release(&b->base);
    res->base.release(&res->base);

    return true;
}

bool ttak_vector_magnitude(ttak_bigreal_t *res, tt_shared_vector_t *v, tt_owner_t *owner, uint64_t tt_now) {
    if (!res || !v || !owner) return false;

    ttak_shared_result_t rv;
    ttak_vector_t *vec = (ttak_vector_t *)v->base.access(&v->base, owner, &rv);
    if (!vec) return false;

    ttak_bigreal_t sum, prod;
    ttak_bigreal_init(&sum, tt_now);
    ttak_bigreal_init(&prod, tt_now);

    for (uint8_t i = 0; i < vec->dim; i++) {
        ttak_bigreal_mul(&prod, &vec->elements[i], &vec->elements[i], tt_now);
        ttak_bigreal_add(&sum, &sum, &prod, tt_now);
    }

    ttak_bigreal_copy(res, &sum, tt_now);

    ttak_bigreal_free(&sum, tt_now);
    ttak_bigreal_free(&prod, tt_now);
    v->base.release(&v->base);

    return true;
}

/**
 * @brief Approximate sine using Yussigihae (Nam Byeong-gil) logic.
 */
bool ttak_math_approx_sin(ttak_bigreal_t *res, const ttak_bigreal_t *x, uint64_t tt_now) {
    ttak_bigreal_t x2, term, tmp, denom;
    ttak_bigreal_init(&x2, tt_now);
    ttak_bigreal_init(&term, tt_now);
    ttak_bigreal_init(&tmp, tt_now);
    ttak_bigreal_init(&denom, tt_now);

    ttak_bigreal_mul(&x2, x, x, tt_now);

    /* term = -1/5040 */
    ttak_bigint_set_u64(&term.mantissa, 1, tt_now); term.exponent = 0;
    ttak_bigint_set_u64(&denom.mantissa, 5040, tt_now); denom.exponent = 0;
    ttak_bigreal_div(&term, &term, &denom, tt_now);
    term.mantissa.is_negative = true;

    /* term = term * x2 + 1/120 */
    ttak_bigreal_mul(&term, &term, &x2, tt_now);
    ttak_bigint_set_u64(&tmp.mantissa, 1, tt_now); tmp.exponent = 0;
    ttak_bigint_set_u64(&denom.mantissa, 120, tt_now); denom.exponent = 0;
    ttak_bigreal_div(&tmp, &tmp, &denom, tt_now);
    ttak_bigreal_add(&term, &term, &tmp, tt_now);

    /* term = term * x2 - 1/6 */
    ttak_bigreal_mul(&term, &term, &x2, tt_now);
    ttak_bigint_set_u64(&tmp.mantissa, 1, tt_now); tmp.exponent = 0;
    ttak_bigint_set_u64(&denom.mantissa, 6, tt_now); denom.exponent = 0;
    ttak_bigreal_div(&tmp, &tmp, &denom, tt_now);
    tmp.mantissa.is_negative = true;
    ttak_bigreal_add(&term, &term, &tmp, tt_now);

    /* res = (term * x2 + 1) * x */
    ttak_bigreal_mul(&term, &term, &x2, tt_now);
    ttak_bigint_set_u64(&tmp.mantissa, 1, tt_now); tmp.exponent = 0;
    ttak_bigreal_add(&term, &term, &tmp, tt_now);
    ttak_bigreal_mul(res, &term, x, tt_now);

    ttak_bigreal_free(&x2, tt_now);
    ttak_bigreal_free(&term, tt_now);
    ttak_bigreal_free(&tmp, tt_now);
    ttak_bigreal_free(&denom, tt_now);
    return true;
}

/**
 * @brief Approximate cosine using Yussigihae (Nam Byeong-gil) logic.
 */
bool ttak_math_approx_cos(ttak_bigreal_t *res, const ttak_bigreal_t *x, uint64_t tt_now) {
    ttak_bigreal_t x2, term, tmp, denom;
    ttak_bigreal_init(&x2, tt_now);
    ttak_bigreal_init(&term, tt_now);
    ttak_bigreal_init(&tmp, tt_now);
    ttak_bigreal_init(&denom, tt_now);

    ttak_bigreal_mul(&x2, x, x, tt_now);

    /* term = -1/720 */
    ttak_bigint_set_u64(&term.mantissa, 1, tt_now); term.exponent = 0;
    ttak_bigint_set_u64(&denom.mantissa, 720, tt_now); denom.exponent = 0;
    ttak_bigreal_div(&term, &term, &denom, tt_now);
    term.mantissa.is_negative = true;

    /* term = term * x2 + 1/24 */
    ttak_bigreal_mul(&term, &term, &x2, tt_now);
    ttak_bigint_set_u64(&tmp.mantissa, 1, tt_now); tmp.exponent = 0;
    ttak_bigint_set_u64(&denom.mantissa, 24, tt_now); denom.exponent = 0;
    ttak_bigreal_div(&tmp, &tmp, &denom, tt_now);
    ttak_bigreal_add(&term, &term, &tmp, tt_now);

    /* term = term * x2 - 1/2 */
    ttak_bigreal_mul(&term, &term, &x2, tt_now);
    ttak_bigint_set_u64(&tmp.mantissa, 1, tt_now); tmp.exponent = 0;
    ttak_bigint_set_u64(&denom.mantissa, 2, tt_now); denom.exponent = 0;
    ttak_bigreal_div(&tmp, &tmp, &denom, tt_now);
    tmp.mantissa.is_negative = true;
    ttak_bigreal_add(&term, &term, &tmp, tt_now);

    /* res = term * x2 + 1 */
    ttak_bigreal_mul(&term, &term, &x2, tt_now);
    ttak_bigint_set_u64(&tmp.mantissa, 1, tt_now); tmp.exponent = 0;
    ttak_bigreal_add(res, &term, &tmp, tt_now);

    ttak_bigreal_free(&x2, tt_now);
    ttak_bigreal_free(&term, tt_now);
    ttak_bigreal_free(&tmp, tt_now);
    ttak_bigreal_free(&denom, tt_now);
    return true;
}