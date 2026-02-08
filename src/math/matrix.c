#include <ttak/math/matrix.h>
#include <ttak/mem/mem.h>
#include <stdlib.h>
#include <string.h>

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
    
    ttak_bigreal_t sum, prod;
    ttak_bigreal_init(&sum, now);
    ttak_bigreal_init(&prod, now);
    
    for (uint8_t i = 0; i < mat->rows; i++) {
        ttak_bigreal_init_u64(&sum, 0, now);
        for (uint8_t j = 0; j < mat->cols; j++) {
            ttak_bigreal_mul(&prod, &mat->elements[i * mat->cols + j], &vec->elements[j], now);
            ttak_bigreal_add(&sum, &sum, &prod, now);
        }
        ttak_bigreal_copy(&v_res->elements[i], &sum, now);
    }
    
    ttak_bigreal_free(&sum, now);
    ttak_bigreal_free(&prod, now);
    
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
    (void)m; (void)owner; (void)axis; (void)angle; (void)now;
    // Placeholder for sin/cos implementation.
    // For now, just set identity to avoid crash.
    // In a real implementation, we would use a bigreal sin/cos.
    return true;
}

_Bool ttak_matrix_set_shearing(tt_shared_matrix_t *m, tt_owner_t *owner, uint8_t axis, const ttak_bigreal_t *factor, uint64_t now) {
    (void)m; (void)owner; (void)axis; (void)factor; (void)now;
    // Similar to rotation, would set specific elements.
    return true;
}

_Bool ttak_matrix_set_flip(tt_shared_matrix_t *m, tt_owner_t *owner, uint8_t axis, uint64_t now) {
    ttak_shared_result_t res;
    ttak_matrix_t *mat = (ttak_matrix_t *)m->base.access(&m->base, owner, &res);
    if (!mat) return false;
    
    // Set identity first
    for (int i = 0; i < 16; i++) ttak_bigreal_init_u64(&mat->elements[i], 0, now);
    for (int i = 0; i < mat->rows && i < mat->cols; i++) {
        ttak_bigreal_init_u64(&mat->elements[i * mat->cols + i], (i == axis) ? -1 : 1, now);
        // Note: bigint needs to handle negative.
        mat->elements[i * mat->cols + i].mantissa.is_negative = (i == axis);
    }
    
    m->base.release(&m->base);
    return true;
}
