#include <inttypes.h>
#include <stdio.h>
#include <ttak/math/sum_divisors.h>

int main(void) {
    uint64_t value = 220;
    uint64_t sum = 0;
    if (ttak_sum_proper_divisors_u64(value, &sum)) {
        printf("sigma(%" PRIu64 ") - %" PRIu64 " = %" PRIu64 "\n", value, value, sum);
    } else {
        puts("sum overflowed 64-bit range");
    }
    return 0;
}
