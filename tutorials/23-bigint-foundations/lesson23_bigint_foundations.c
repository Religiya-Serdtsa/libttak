#include <inttypes.h>
#include <stdio.h>
#include <ttak/math/bigint.h>
#include <ttak/timing/timing.h>

int main(void) {
    uint64_t now = ttak_get_tick_count();
    ttak_bigint_t lhs, rhs, sum;
    ttak_bigint_init_u64(&lhs, 1234567890123456ULL, now);
    ttak_bigint_init_u64(&rhs, 987654321ULL, now);
    ttak_bigint_init(&sum, now);
    ttak_bigint_add(&sum, &lhs, &rhs, now);
    char hex[65];
    ttak_bigint_to_hex_hash(&sum, hex);
    printf("lhs + rhs hex hash = %s\n", hex);
    ttak_bigint_free(&lhs, now);
    ttak_bigint_free(&rhs, now);
    ttak_bigint_free(&sum, now);
    return 0;
}
