#include <ttak/math/bigreal.h>
#include <ttak/mem/mem.h>
#include <string.h>

/**
 * @brief Initialize a big real by zeroing its mantissa and exponent.
 *
 * @param br  Big real to initialize.
 * @param now Timestamp forwarded to bigint init.
 */
void ttak_bigreal_init(ttak_bigreal_t *br, uint64_t now) {
    ttak_bigint_init(&br->mantissa, now);
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
    if (lhs->exponent != rhs->exponent) {
        return false;
    }
    dst->exponent = lhs->exponent;
    return ttak_bigint_add(&dst->mantissa, &lhs->mantissa, &rhs->mantissa, now);
}
