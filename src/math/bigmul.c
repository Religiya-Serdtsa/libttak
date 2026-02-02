#include <ttak/math/bigmul.h>

/**
 * @brief Initialize a big multiplication accumulator.
 *
 * @param bm  Structure to initialize.
 * @param now Timestamp propagated to bigint initializers.
 */
void ttak_bigmul_init(ttak_bigmul_t *bm, uint64_t now) {
    ttak_bigint_init(&bm->lhs, now);
    ttak_bigint_init(&bm->rhs, now);
    ttak_bigint_init(&bm->product, now);
}

/**
 * @brief Release all big integers owned by the accumulator.
 *
 * @param bm  Structure to destroy.
 * @param now Timestamp forwarded to bigint destructors.
 */
void ttak_bigmul_free(ttak_bigmul_t *bm, uint64_t now) {
    ttak_bigint_free(&bm->lhs, now);
    ttak_bigint_free(&bm->rhs, now);
    ttak_bigint_free(&bm->product, now);
}
