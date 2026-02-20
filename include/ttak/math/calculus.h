#ifndef TTAK_MATH_CALCULUS_H
#define TTAK_MATH_CALCULUS_H

#include <ttak/math/bigreal.h>
#include <ttak/priority/scheduler.h>

/**
 * @brief Function signature for numerical methods.
 * @param res Destination for the function value.
 * @param x Input value.
 * @param ctx User context.
 * @param now Timestamp.
 * @return true on success.
 */
typedef _Bool (*ttak_math_func_t)(ttak_bigreal_t *res, const ttak_bigreal_t *x, void *ctx, uint64_t now);

/**
 * @brief Numerical differentiation at point x.
 */
_Bool ttak_calculus_diff(ttak_bigreal_t *res, ttak_math_func_t f, const ttak_bigreal_t *x, void *ctx, uint64_t now);

/**
 * @brief Partial differentiation at point x for a specific dimension.
 */
_Bool ttak_calculus_partial_diff(ttak_bigreal_t *res, ttak_math_func_t f, const ttak_bigreal_t *x_vec, uint8_t dim, void *ctx, uint64_t now);

/**
 * @brief Numerical definite integration over [a, b].
 * Uses adaptive methods and can be parallelized.
 */
_Bool ttak_calculus_integrate(ttak_bigreal_t *res, ttak_math_func_t f, const ttak_bigreal_t *a, const ttak_bigreal_t *b, void *ctx, uint64_t now);

/**
 * @brief RK4 (Runge-Kutta 4th Order) ODE solver step.
 * Solves dy/dt = f(t, y)
 * @param y_next Next state.
 * @param f Derivative function.
 * @param t Current time.
 * @param y Current state.
 * @param h Time step.
 * @param ctx User context.
 * @param now Timestamp.
 * @return true on success.
 */
_Bool ttak_calculus_rk4_step(ttak_bigreal_t *y_next, ttak_math_func_t f, const ttak_bigreal_t *t, const ttak_bigreal_t *y, const ttak_bigreal_t *h, void *ctx, uint64_t now);

#endif // TTAK_MATH_CALCULUS_H
