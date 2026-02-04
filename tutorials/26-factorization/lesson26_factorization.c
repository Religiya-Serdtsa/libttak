#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <ttak/math/factor.h>
#include <ttak/timing/timing.h>

int main(void) {
    uint64_t now = ttak_get_tick_count();
    ttak_prime_factor_t *factors = NULL;
    size_t count = 0;
    if (ttak_factor_u64(360, &factors, &count, now) == 0) {
        printf("360 factors: ");
        for (size_t i = 0; i < count; ++i) {
            printf("%" PRIu64 "^%u ", factors[i].p, factors[i].a);
        }
        putchar('
');
    }
    free(factors);
    return 0;
}
