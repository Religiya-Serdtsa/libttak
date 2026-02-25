#ifndef TTAK_MATH_VECTOR_H
#define TTAK_MATH_VECTOR_H

#include <stdint.h>
#include <ttak/math/bigreal.h>
#include <ttak/shared/shared.h>
#include <ttak/mem/owner.h>

/**
 * @brief Vector structure for 2D, 3D, and 4D.
 */
typedef struct ttak_vector {
    uint8_t dim;
    ttak_bigreal_t elements[4];
} ttak_vector_t;

TTAK_SHARED_DEFINE_WRAPPER(vector, ttak_vector_t)
typedef ttak_shared_vector_t tt_shared_vector_t;

/**
 * @brief Creates a shared vector.
 */
tt_shared_vector_t* ttak_vector_create(uint8_t dim, tt_owner_t *owner, uint64_t now);

/**
 * @brief Destroys a shared vector.
 */
void ttak_vector_destroy(tt_shared_vector_t *sv, uint64_t now);

/**
 * @brief Safe access to an element. Validates owner and bounds.
 */
ttak_bigreal_t* ttak_vector_get(tt_shared_vector_t *sv, tt_owner_t *owner, uint8_t index, uint64_t now);

/**
 * @brief Sets an element value.
 */
_Bool ttak_vector_set(tt_shared_vector_t *sv, tt_owner_t *owner, uint8_t index, const ttak_bigreal_t *val, uint64_t now);

/**
 * @brief Vector Dot Product.
 */
_Bool ttak_vector_dot(ttak_bigreal_t *res, tt_shared_vector_t *a, tt_shared_vector_t *b, tt_owner_t *owner, uint64_t now);

/**
 * @brief Vector Cross Product (3D only).
 */
_Bool ttak_vector_cross(tt_shared_vector_t *res, tt_shared_vector_t *a, tt_shared_vector_t *b, tt_owner_t *owner, uint64_t now);

/**
 * @brief Magnitude of the vector.
 */
_Bool ttak_vector_magnitude(ttak_bigreal_t *res, tt_shared_vector_t *v, tt_owner_t *owner, uint64_t now);

/**
 * @brief Approximate sine function using Yussigihae-inspired polynomial expansion.
 *
 * @param res Result bigreal.
 * @param angle Angle in radians.
 * @param now Timestamp.
 * @return true on success.
 */
_Bool ttak_math_approx_sin(ttak_bigreal_t *res, const ttak_bigreal_t *angle, uint64_t now);

/**
 * @brief Approximate cosine function using Yussigihae-inspired polynomial expansion.
 *
 * @param res Result bigreal.
 * @param angle Angle in radians.
 * @param now Timestamp.
 * @return true on success.
 */
_Bool ttak_math_approx_cos(ttak_bigreal_t *res, const ttak_bigreal_t *angle, uint64_t now);

#endif // TTAK_MATH_VECTOR_H
