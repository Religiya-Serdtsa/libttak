#ifndef TTAK_MATH_BIGINT_ACCEL_H
#define TTAK_MATH_BIGINT_ACCEL_H

#include <stddef.h>
#include <stdbool.h>
#include <ttak/math/bigint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns true when a GPU backend capable of BigInt operations is available.
 */
bool ttak_bigint_accel_available(void);

/**
 * @brief Returns the minimum operand limb count required before acceleration is attempted.
 */
size_t ttak_bigint_accel_min_limbs(void);

/**
 * @brief Performs limb-wise addition via an accelerator backend.
 *
 * @param dst          Destination limb buffer (device result copied to host).
 * @param dst_capacity Number of limbs available in @p dst.
 * @param out_used     Optional pointer to receive the number of limbs used.
 * @param lhs          Left operand limbs.
 * @param lhs_used     Number of limbs in @p lhs.
 * @param rhs          Right operand limbs.
 * @param rhs_used     Number of limbs in @p rhs.
 * @return true when the backend executed successfully, false when unsupported/failing.
 */
bool ttak_bigint_accel_add_raw(limb_t *dst,
                               size_t dst_capacity,
                               size_t *out_used,
                               const limb_t *lhs,
                               size_t lhs_used,
                               const limb_t *rhs,
                               size_t rhs_used);

/**
 * @brief Performs limb-wise multiplication via an accelerator backend.
 */
bool ttak_bigint_accel_mul_raw(limb_t *dst,
                               size_t dst_capacity,
                               size_t *out_used,
                               const limb_t *lhs,
                               size_t lhs_used,
                               const limb_t *rhs,
                               size_t rhs_used);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_MATH_BIGINT_ACCEL_H */
