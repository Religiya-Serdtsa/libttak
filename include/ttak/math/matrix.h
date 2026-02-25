#ifndef TTAK_MATH_MATRIX_H
#define TTAK_MATH_MATRIX_H

#include <ttak/math/vector.h>

/**
 * @brief Matrix structure for up to 4x4.
 */
typedef struct ttak_matrix {
    uint8_t rows;
    uint8_t cols;
    ttak_bigreal_t elements[16];
} ttak_matrix_t;

TTAK_SHARED_DEFINE_WRAPPER(matrix, ttak_matrix_t)
typedef ttak_shared_matrix_t tt_shared_matrix_t;

/**
 * @brief Creates a shared matrix.
 */
tt_shared_matrix_t* ttak_matrix_create(uint8_t rows, uint8_t cols, tt_owner_t *owner, uint64_t now);

/**
 * @brief Safe access to an element (row, col).
 */
ttak_bigreal_t* ttak_matrix_get(tt_shared_matrix_t *sm, tt_owner_t *owner, uint8_t row, uint8_t col, uint64_t now);

/**
 * @brief Sets an element value.
 */
_Bool ttak_matrix_set(tt_shared_matrix_t *sm, tt_owner_t *owner, uint8_t row, uint8_t col, const ttak_bigreal_t *val, uint64_t now);

/**
 * @brief Matrix-Vector Multiplication.
 */
_Bool ttak_matrix_multiply_vec(tt_shared_vector_t *res, tt_shared_matrix_t *m, tt_shared_vector_t *v, tt_owner_t *owner, uint64_t now);

/**
 * @brief Matrix-Matrix Multiplication.
 */
_Bool ttak_matrix_multiply(tt_shared_matrix_t *res, tt_shared_matrix_t *a, tt_shared_matrix_t *b, tt_owner_t *owner, uint64_t now);

/**
 * @brief Transformation: Rotation (2D around origin, 3D around axis).
 */
_Bool ttak_matrix_set_rotation(tt_shared_matrix_t *m, tt_owner_t *owner, uint8_t axis, const ttak_bigreal_t *angle, uint64_t now);

/**
 * @brief Transformation: Shearing.
 */
_Bool ttak_matrix_set_shearing(tt_shared_matrix_t *m, tt_owner_t *owner, uint8_t axis, const ttak_bigreal_t *factor, uint64_t now);

/**
 * @brief Transformation: Axis Flipping.
 */
_Bool ttak_matrix_set_flip(tt_shared_matrix_t *m, tt_owner_t *owner, uint8_t axis, uint64_t now);

/**
 * @brief Initializes the matrix as a 4x4 Orthogonal Latin Square.
 *
 * Inspired by Choi Seok-jeong's Gusuryak, this creates a pattern that
 * minimizes collisions in grid-based data placement.
 */
_Bool ttak_matrix_set_gusuryak_4x4(tt_shared_matrix_t *m, tt_owner_t *owner, uint64_t now);

#endif // TTAK_MATH_MATRIX_H
