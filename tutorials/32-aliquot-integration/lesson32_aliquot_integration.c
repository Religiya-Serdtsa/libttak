#define _XOPEN_SOURCE 700
#include <inttypes.h>
#include <stdio.h>
#include <ttak/limit/limit.h>
#include <ttak/math/sum_divisors.h>

int main(void) {
    ttak_ratelimit_t rl;
    ttak_ratelimit_init(&rl, 10.0, 1.0);
    for (uint64_t n = 200; n < 205; ++n) {
        if (!ttak_ratelimit_allow(&rl)) {
            puts("rate limited sample, pause before next query");
            continue;
        }
        uint64_t sum = 0;
        ttak_sum_proper_divisors_u64(n, &sum);
        printf("aliquot(%" PRIu64 ") = %" PRIu64 "\n", n, sum);
    }
    return 0;
}
