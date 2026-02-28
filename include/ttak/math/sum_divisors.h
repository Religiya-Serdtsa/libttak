#ifndef TTAK_MATH_SUM_DIVISORS_H
#define TTAK_MATH_SUM_DIVISORS_H

#include <ttak/log/logger.h>
#include <ttak/math/bigint.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TTAK_SUMDIV_BIG_ERROR_NONE = 0,
    TTAK_SUMDIV_BIG_ERROR_FACTOR,
    TTAK_SUMDIV_BIG_ERROR_EXPORT,
    TTAK_SUMDIV_BIG_ERROR_SET_VALUE,
    TTAK_SUMDIV_BIG_ERROR_ARITHMETIC,
    TTAK_SUMDIV_BIG_ERROR_GENERIC,
    TTAK_SUMDIV_BIG_ERROR_INPUT_TOO_LARGE
} ttak_sumdiv_big_error_t;

/**
 * @brief Calculates the sum of proper divisors for a 64-bit unsigned integer.
 *
 * This function calculates sigma(n) - n. It uses 128-bit intermediate
 * arithmetic to detect overflow.
 *
 * @param n The number.
 * @param result_out A pointer to store the sum of proper divisors.
 * @return `true` if the calculation was successful and fits in a uint64_t.
 *         `false` if the sum of divisors overflows uint64_t. In this case,
 *         the value in result_out is undefined.
 */
bool ttak_sum_proper_divisors_u64(uint64_t n, uint64_t *result_out);

/**
 * @brief Calculates the sum of proper divisors for a big integer.
 *
 * @param n The bigint number.
 * @param result_out The bigint to store the sum of proper divisors.
 * @param now The current timestamp for memory allocation.
 * @return `true` on success, `false` on memory allocation failure.
 */
bool ttak_sum_proper_divisors_big(const ttak_bigint_t *n, ttak_bigint_t *result_out, uint64_t now);

/**
 * @brief Returns the last failure reason emitted by ttak_sum_proper_divisors_big().
 */
ttak_sumdiv_big_error_t ttak_sum_proper_divisors_big_last_error(void);

/**
 * @brief Returns a short symbolic name for a ttak_sumdiv_big_error_t value.
 */
const char *ttak_sum_proper_divisors_big_error_name(ttak_sumdiv_big_error_t err);

/**
 * @brief Attaches a logger for auto-heal stage reporting.
 */
void ttak_sum_divisors_attach_logger(ttak_logger_t *logger);

typedef struct {
    size_t max_input_bits; /**< Fail fast when n exceeds this bit-length (0 disables limit). */
} ttak_sumdiv_limits_t;

void ttak_sum_divisors_set_limits(const ttak_sumdiv_limits_t *limits);
void ttak_sum_divisors_get_limits(ttak_sumdiv_limits_t *limits_out);

#endif // TTAK_MATH_SUM_DIVISORS_H
