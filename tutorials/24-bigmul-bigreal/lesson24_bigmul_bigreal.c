#include <stdio.h>
#include <ttak/math/bigmul.h>
#include <ttak/math/bigreal.h>
#include <ttak/timing/timing.h>

int main(void) {
    uint64_t now = ttak_get_tick_count();
    ttak_bigmul_t bm;
    ttak_bigmul_init(&bm, now);
    ttak_bigint_set_u64(&bm.lhs, 1234, now);
    ttak_bigint_set_u64(&bm.rhs, 5678, now);
    ttak_bigint_mul(&bm.product, &bm.lhs, &bm.rhs, now);
    puts("bigmul product ready for convolution checks");

    ttak_bigreal_t a, b, sum;
    ttak_bigreal_init(&a, now);
    ttak_bigreal_init(&b, now);
    ttak_bigreal_init(&sum, now);
    ttak_bigint_set_u64(&a.mantissa, 314, now);
    a.exponent = -2;
    ttak_bigint_set_u64(&b.mantissa, 159, now);
    b.exponent = -2;
    if (ttak_bigreal_add(&sum, &a, &b, now)) {
        printf("bigreal sum exponent: %lld\n", (long long)sum.exponent);
    }
    ttak_bigreal_free(&a, now);
    ttak_bigreal_free(&b, now);
    ttak_bigreal_free(&sum, now);
    ttak_bigmul_free(&bm, now);
    return 0;
}
