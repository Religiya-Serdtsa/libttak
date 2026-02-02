#include <ttak/math/bigcomplex.h>
#include <ttak/mem/mem.h>

/**
 * @brief Initialize the real and imaginary components of a complex number.
 *
 * @param bc  Complex structure to initialize.
 * @param now Timestamp for memory tracking.
 */
void ttak_bigcomplex_init(ttak_bigcomplex_t *bc, uint64_t now) {
    ttak_bigreal_init(&bc->real, now);
    ttak_bigreal_init(&bc->imag, now);
}

/**
 * @brief Release resources associated with a complex number.
 *
 * @param bc  Complex structure to tear down.
 * @param now Timestamp for memory tracking.
 */
void ttak_bigcomplex_free(ttak_bigcomplex_t *bc, uint64_t now) {
    ttak_bigreal_free(&bc->real, now);
    ttak_bigreal_free(&bc->imag, now);
}

/**
 * @brief Add two complex numbers and store the result in dst.
 *
 * @param dst Destination complex number.
 * @param lhs Left-hand operand.
 * @param rhs Right-hand operand.
 * @param now Timestamp for memory validation.
 * @return true on success, false if an intermediate operation fails.
 */
_Bool ttak_bigcomplex_add(ttak_bigcomplex_t *dst, const ttak_bigcomplex_t *lhs, const ttak_bigcomplex_t *rhs, uint64_t now) {
    if (!ttak_bigreal_add(&dst->real, &lhs->real, &rhs->real, now)) {
        return false;
    }
    if (!ttak_bigreal_add(&dst->imag, &lhs->imag, &rhs->imag, now)) {
        return false;
    }
    return true;
}
