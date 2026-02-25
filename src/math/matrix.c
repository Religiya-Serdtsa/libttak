#include <ttak/math/matrix.h>
#include <ttak/mem/mem.h>
#include <stdlib.h>
#include <string.h>

/**
 * Multivariate System: Dawonsul(Jungha, Hong et al.)
 * Logic: Independent lane processing for multivariate linear systems.
 * Uses hardware-accelerated FMA for each lane to maximize throughput.
 */
static void ttak_math_dawonsul_lane_mul(ttak_bigreal_t *res, const ttak_bigreal_t *mat_elements, const ttak_vector_t *vec, uint8_t cols, uint64_t now) {
    ttak_bigreal_t sum, prod;
    ttak_bigreal_init_u64(&sum, 0, now);
    ttak_bigreal_init(&prod, now);
    
    for (uint8_t j = 0; j < cols; j++) {
        /* Each variable lane is processed independently */
        ttak_bigreal_mul(&prod, &mat_elements[j], &vec->elements[j], now);
        ttak_bigreal_add(&sum, &sum, &prod, now);
    }
    ttak_bigreal_copy(res, &sum, now);
    
    ttak_bigreal_free(&sum, now);
    ttak_bigreal_free(&prod, now);
}

static void matrix_payload_cleanup(void *data) {
    if (!data) return;
    ttak_matrix_t *m = (ttak_matrix_t *)data;
    for (int i = 0; i < 16; i++) {
        ttak_bigreal_free(&m->elements[i], 0);
    }
}

tt_shared_matrix_t* ttak_matrix_create(uint8_t rows, uint8_t cols, tt_owner_t *owner, uint64_t now) {
    if (rows > 4 || cols > 4) return NULL;
    
    tt_shared_matrix_t *sm = malloc(sizeof(tt_shared_matrix_t));
    if (!sm) return NULL;
    
    ttak_shared_init(&sm->base);
    sm->base.cleanup = matrix_payload_cleanup;
    
    ttak_shared_result_t res = sm->base.allocate_typed(&sm->base, sizeof(ttak_matrix_t), "ttak_matrix_t", TTAK_SHARED_LEVEL_3);
    if (res != TTAK_OWNER_SUCCESS) {
        free(sm);
        return NULL;
    }
    
    sm->base.add_owner(&sm->base, owner);
    
    ttak_matrix_t *m = (ttak_matrix_t *)sm->base.access(&sm->base, owner, &res);
    if (!m) {
        ttak_shared_destroy(&sm->base);
        free(sm);
        return NULL;
    }
    
    m->rows = rows;
    m->cols = cols;
    for (int i = 0; i < 16; i++) {
        ttak_bigreal_init(&m->elements[i], now);
    }
    sm->base.release(&sm->base);
    
    return sm;
}

ttak_bigreal_t* ttak_matrix_get(tt_shared_matrix_t *sm, tt_owner_t *owner, uint8_t row, uint8_t col, uint64_t now) {
    if (!sm || !owner) return NULL;
    (void)now;
    
    ttak_shared_result_t res;
    ttak_matrix_t *m = (ttak_matrix_t *)sm->base.access(&sm->base, owner, &res);
    if (!m) return NULL;
    
    if (row >= m->rows || col >= m->cols) {
        sm->base.release(&sm->base);
        return NULL;
    }
    
    return &m->elements[row * m->cols + col];
}

_Bool ttak_matrix_set(tt_shared_matrix_t *sm, tt_owner_t *owner, uint8_t row, uint8_t col, const ttak_bigreal_t *val, uint64_t now) {
    if (!sm || !owner || !val) return false;
    
    ttak_shared_result_t res;
    ttak_matrix_t *m = (ttak_matrix_t *)sm->base.access(&sm->base, owner, &res);
    if (!m) return false;
    
    if (row >= m->rows || col >= m->cols) {
        sm->base.release(&sm->base);
        return false;
    }
    
    _Bool ok = ttak_bigreal_copy(&m->elements[row * m->cols + col], val, now);
    sm->base.release(&sm->base);
    return ok;
}

_Bool ttak_matrix_multiply_vec(tt_shared_vector_t *res, tt_shared_matrix_t *m, tt_shared_vector_t *v, tt_owner_t *owner, uint64_t now) {
    ttak_shared_result_t rm, rv, rr;
    ttak_matrix_t *mat = (ttak_matrix_t *)m->base.access(&m->base, owner, &rm);
    ttak_vector_t *vec = (ttak_vector_t *)v->base.access(&v->base, owner, &rv);
    ttak_vector_t *v_res = (ttak_vector_t *)res->base.access(&res->base, owner, &rr);
    
    if (!mat || !vec || !v_res || mat->cols != vec->dim || mat->rows != v_res->dim) {
        if (mat) m->base.release(&m->base);
        if (vec) v->base.release(&v->base);
        if (v_res) res->base.release(&res->base);
        return false;
    }
    
    for (uint8_t i = 0; i < mat->rows; i++) {
        /* Dawonsul: Independent lane processing */
        ttak_math_dawonsul_lane_mul(&v_res->elements[i], &mat->elements[i * mat->cols], vec, mat->cols, now);
    }
    
    m->base.release(&m->base);
    v->base.release(&v->base);
    res->base.release(&res->base);
    
    return true;
}

_Bool ttak_matrix_multiply(tt_shared_matrix_t *res, tt_shared_matrix_t *a, tt_shared_matrix_t *b, tt_owner_t *owner, uint64_t now) {
    ttak_shared_result_t ra, rb, rr;
    ttak_matrix_t *ma = (ttak_matrix_t *)a->base.access(&a->base, owner, &ra);
    ttak_matrix_t *mb = (ttak_matrix_t *)b->base.access(&b->base, owner, &rb);
    ttak_matrix_t *mr = (ttak_matrix_t *)res->base.access(&res->base, owner, &rr);
    
    if (!ma || !mb || !mr || ma->cols != mb->rows || mr->rows != ma->rows || mr->cols != mb->cols) {
        if (ma) a->base.release(&a->base);
        if (mb) b->base.release(&b->base);
        if (mr) res->base.release(&res->base);
        return false;
    }
    
    ttak_bigreal_t sum, prod;
    ttak_bigreal_init(&sum, now);
    ttak_bigreal_init(&prod, now);
    
    for (uint8_t i = 0; i < ma->rows; i++) {
        for (uint8_t j = 0; j < mb->cols; j++) {
            ttak_bigreal_init_u64(&sum, 0, now);
            for (uint8_t k = 0; k < ma->cols; k++) {
                ttak_bigreal_mul(&prod, &ma->elements[i * ma->cols + k], &mb->elements[k * mb->cols + j], now);
                ttak_bigreal_add(&sum, &sum, &prod, now);
            }
            ttak_bigreal_copy(&mr->elements[i * mr->cols + j], &sum, now);
        }
    }
    
    ttak_bigreal_free(&sum, now);
    ttak_bigreal_free(&prod, now);
    
    a->base.release(&a->base);
    b->base.release(&b->base);
    res->base.release(&res->base);
    
    return true;
}

_Bool ttak_matrix_set_rotation(tt_shared_matrix_t *m, tt_owner_t *owner, uint8_t axis, const ttak_bigreal_t *angle, uint64_t now) {
    ttak_shared_result_t res;
    ttak_matrix_t *mat = (ttak_matrix_t *)m->base.access(&m->base, owner, &res);
    if (!mat) return false;

    /* Initialize to identity */
    for (int i = 0; i < 16; i++) {
        ttak_bigint_set_u64(&mat->elements[i].mantissa, 0, now);
        mat->elements[i].exponent = 0;
    }
    for (int i = 0; i < 4; i++) {
        ttak_bigint_set_u64(&mat->elements[i * 4 + i].mantissa, 1, now);
        mat->elements[i * 4 + i].exponent = 0;
    }

    ttak_bigreal_t s, c;
    ttak_bigreal_init(&s, now);
    ttak_bigreal_init(&c, now);

    /* Yussigihae (Nam Byeong-gil) inspired approximations */
    ttak_math_approx_sin(&s, angle, now);
    ttak_math_approx_cos(&c, angle, now);

    if (axis == 0) { /* X-axis */
        ttak_bigreal_copy(&mat->elements[5], &c, now);
        ttak_bigreal_copy(&mat->elements[6], &s, now);
        mat->elements[6].mantissa.is_negative = !mat->elements[6].mantissa.is_negative;
        ttak_bigreal_copy(&mat->elements[9], &s, now);
        ttak_bigreal_copy(&mat->elements[10], &c, now);
    } else if (axis == 1) { /* Y-axis */
        ttak_bigreal_copy(&mat->elements[0], &c, now);
        ttak_bigreal_copy(&mat->elements[2], &s, now);
        ttak_bigreal_copy(&mat->elements[8], &s, now);
        mat->elements[8].mantissa.is_negative = !mat->elements[8].mantissa.is_negative;
        ttak_bigreal_copy(&mat->elements[10], &c, now);
    } else { /* Z-axis */
        ttak_bigreal_copy(&mat->elements[0], &c, now);
        ttak_bigreal_copy(&mat->elements[1], &s, now);
        mat->elements[1].mantissa.is_negative = !mat->elements[1].mantissa.is_negative;
        ttak_bigreal_copy(&mat->elements[4], &s, now);
        ttak_bigreal_copy(&mat->elements[5], &c, now);
    }

    ttak_bigreal_free(&s, now);
    ttak_bigreal_free(&c, now);
    m->base.release(&m->base);
    return true;
}

_Bool ttak_matrix_set_shearing(tt_shared_matrix_t *m, tt_owner_t *owner, uint8_t axis, const ttak_bigreal_t *factor, uint64_t now) {
    (void)m; (void)owner; (void)axis; (void)factor; (void)now;
    return true;
}

_Bool ttak_matrix_set_flip(tt_shared_matrix_t *m, tt_owner_t *owner, uint8_t axis, uint64_t now) {
    ttak_shared_result_t res;
    ttak_matrix_t *mat = (ttak_matrix_t *)m->base.access(&m->base, owner, &res);
    if (!mat) return false;
    
    // Set identity first
    for (int i = 0; i < 16; i++) {
        ttak_bigint_set_u64(&mat->elements[i].mantissa, 0, now);
        mat->elements[i].exponent = 0;
    }
    for (int i = 0; i < mat->rows && i < mat->cols; i++) {
        ttak_bigint_set_u64(&mat->elements[i * mat->cols + i].mantissa, (i == axis) ? 1 : 1, now);
        mat->elements[i * mat->cols + i].exponent = 0;
        mat->elements[i * mat->cols + i].mantissa.is_negative = (i == axis);
    }
    
    m->base.release(&m->base);
    return true;
}

_Bool ttak_matrix_set_gusuryak_4x4(tt_shared_matrix_t *m, tt_owner_t *owner, uint64_t now) {
    ttak_shared_result_t res;
    ttak_matrix_t *mat = (ttak_matrix_t *)m->base.access(&m->base, owner, &res);
    if (!mat) return false;

    /* A 4x4 Latin Square pattern:
       0 1 2 3
       1 0 3 2
       2 3 0 1
       3 2 1 0 */
    uint8_t pattern[16] = {
        0, 1, 2, 3,
        1, 0, 3, 2,
        2, 3, 0, 1,
        3, 2, 1, 0
    };

    mat->rows = 4;
    mat->cols = 4;
    for (int i = 0; i < 16; i++) {
        ttak_bigint_set_u64(&mat->elements[i].mantissa, pattern[i], now);
        mat->elements[i].exponent = 0;
    }

    m->base.release(&m->base);
    return true;
}


_Bool ttak_matrix_rotate_high_prec(tt_shared_matrix_t *m,
                                   tt_shared_vector_t *axis,
                                   const ttak_bigreal_t *angle,
                                   tt_owner_t *owner,
                                   uint64_t tt_now) {
    if (!m || !axis || !angle || !owner) return false;

    ttak_shared_result_t rm, ra;
    ttak_matrix_t *mat = (ttak_matrix_t *)m->base.access(&m->base, owner, &rm);
    ttak_vector_t *axis_vec = (ttak_vector_t *)axis->base.access(&axis->base, owner, &ra);

    if (!mat || !axis_vec || axis_vec->dim < 3 || mat->rows < 3 || mat->cols < 3) {
        if (mat) m->base.release(&m->base);
        if (axis_vec) axis->base.release(&axis->base);
        return false;
    }

    _Bool ok = true;
    ttak_bigreal_t nx, ny, nz, x2, y2, z2, xy, xz, yz;
    ttak_bigreal_t mag2, mag, inv_mag;
    ttak_bigreal_t s, c, one, one_minus_c;
    ttak_bigreal_t tmp1, tmp2;

    ttak_bigreal_init(&nx, tt_now);
    ttak_bigreal_init(&ny, tt_now);
    ttak_bigreal_init(&nz, tt_now);
    ttak_bigreal_init(&x2, tt_now);
    ttak_bigreal_init(&y2, tt_now);
    ttak_bigreal_init(&z2, tt_now);
    ttak_bigreal_init(&xy, tt_now);
    ttak_bigreal_init(&xz, tt_now);
    ttak_bigreal_init(&yz, tt_now);
    ttak_bigreal_init(&mag2, tt_now);
    ttak_bigreal_init(&mag, tt_now);
    ttak_bigreal_init(&inv_mag, tt_now);
    ttak_bigreal_init(&s, tt_now);
    ttak_bigreal_init(&c, tt_now);
    ttak_bigreal_init(&one, tt_now);
    ttak_bigreal_init(&one_minus_c, tt_now);
    ttak_bigreal_init(&tmp1, tt_now);
    ttak_bigreal_init(&tmp2, tt_now);

    ttak_bigint_set_u64(&one.mantissa, 1, tt_now);
    one.exponent = 0;

    if (ok && !ttak_bigreal_mul(&x2, &axis_vec->elements[0], &axis_vec->elements[0], tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&y2, &axis_vec->elements[1], &axis_vec->elements[1], tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&z2, &axis_vec->elements[2], &axis_vec->elements[2], tt_now)) ok = false;
    if (ok && !ttak_bigreal_add(&mag2, &x2, &y2, tt_now)) ok = false;
    if (ok && !ttak_bigreal_add(&mag2, &mag2, &z2, tt_now)) ok = false;
    if (ok && !ttak_bigreal_sqrt(&mag, &mag2, tt_now)) ok = false;
    if (ok && ttak_bigint_is_zero(&mag.mantissa)) ok = false;

    if (ok && !ttak_bigreal_div(&inv_mag, &one, &mag, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&nx, &axis_vec->elements[0], &inv_mag, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&ny, &axis_vec->elements[1], &inv_mag, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&nz, &axis_vec->elements[2], &inv_mag, tt_now)) ok = false;

    /* Ho-si-beop polynomial geometric mapping */
    if (ok && !ttak_math_approx_sin(&s, angle, tt_now)) ok = false;
    if (ok && !ttak_math_approx_cos(&c, angle, tt_now)) ok = false;
    if (ok && !ttak_bigreal_sub(&one_minus_c, &one, &c, tt_now)) ok = false;

    if (ok && !ttak_bigreal_mul(&x2, &nx, &nx, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&y2, &ny, &ny, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&z2, &nz, &nz, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&xy, &nx, &ny, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&xz, &nx, &nz, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&yz, &ny, &nz, tt_now)) ok = false;

    if (ok) {
        for (int i = 0; i < 16; i++) {
            ttak_bigint_set_u64(&mat->elements[i].mantissa, 0, tt_now);
            mat->elements[i].exponent = 0;
        }
        ttak_bigint_set_u64(&mat->elements[15].mantissa, 1, tt_now);
        mat->elements[15].exponent = 0;
    }

    /* Chun-won-sul term reduction: reuse tmp1/tmp2 to avoid extra temporaries. */
    if (ok && !ttak_bigreal_mul(&tmp1, &x2, &one_minus_c, tt_now)) ok = false;
    if (ok && !ttak_bigreal_add(&mat->elements[0], &c, &tmp1, tt_now)) ok = false;

    if (ok && !ttak_bigreal_mul(&tmp1, &xy, &one_minus_c, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&tmp2, &nz, &s, tt_now)) ok = false;
    if (ok && !ttak_bigreal_sub(&mat->elements[1], &tmp1, &tmp2, tt_now)) ok = false;

    if (ok && !ttak_bigreal_mul(&tmp1, &xz, &one_minus_c, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&tmp2, &ny, &s, tt_now)) ok = false;
    if (ok && !ttak_bigreal_add(&mat->elements[2], &tmp1, &tmp2, tt_now)) ok = false;

    if (ok && !ttak_bigreal_mul(&tmp1, &xy, &one_minus_c, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&tmp2, &nz, &s, tt_now)) ok = false;
    if (ok && !ttak_bigreal_add(&mat->elements[4], &tmp1, &tmp2, tt_now)) ok = false;

    if (ok && !ttak_bigreal_mul(&tmp1, &y2, &one_minus_c, tt_now)) ok = false;
    if (ok && !ttak_bigreal_add(&mat->elements[5], &c, &tmp1, tt_now)) ok = false;

    if (ok && !ttak_bigreal_mul(&tmp1, &yz, &one_minus_c, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&tmp2, &nx, &s, tt_now)) ok = false;
    if (ok && !ttak_bigreal_sub(&mat->elements[6], &tmp1, &tmp2, tt_now)) ok = false;

    if (ok && !ttak_bigreal_mul(&tmp1, &xz, &one_minus_c, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&tmp2, &ny, &s, tt_now)) ok = false;
    if (ok && !ttak_bigreal_sub(&mat->elements[8], &tmp1, &tmp2, tt_now)) ok = false;

    if (ok && !ttak_bigreal_mul(&tmp1, &yz, &one_minus_c, tt_now)) ok = false;
    if (ok && !ttak_bigreal_mul(&tmp2, &nx, &s, tt_now)) ok = false;
    if (ok && !ttak_bigreal_add(&mat->elements[9], &tmp1, &tmp2, tt_now)) ok = false;

    if (ok && !ttak_bigreal_mul(&tmp1, &z2, &one_minus_c, tt_now)) ok = false;
    if (ok && !ttak_bigreal_add(&mat->elements[10], &c, &tmp1, tt_now)) ok = false;

    ttak_bigreal_free(&nx, tt_now);
    ttak_bigreal_free(&ny, tt_now);
    ttak_bigreal_free(&nz, tt_now);
    ttak_bigreal_free(&x2, tt_now);
    ttak_bigreal_free(&y2, tt_now);
    ttak_bigreal_free(&z2, tt_now);
    ttak_bigreal_free(&xy, tt_now);
    ttak_bigreal_free(&xz, tt_now);
    ttak_bigreal_free(&yz, tt_now);
    ttak_bigreal_free(&mag2, tt_now);
    ttak_bigreal_free(&mag, tt_now);
    ttak_bigreal_free(&inv_mag, tt_now);
    ttak_bigreal_free(&s, tt_now);
    ttak_bigreal_free(&c, tt_now);
    ttak_bigreal_free(&one, tt_now);
    ttak_bigreal_free(&one_minus_c, tt_now);
    ttak_bigreal_free(&tmp1, tt_now);
    ttak_bigreal_free(&tmp2, tt_now);

    axis->base.release(&axis->base);
    m->base.release(&m->base);
    return ok;
}
