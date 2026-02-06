#define _XOPEN_SOURCE 700
#include <inttypes.h>
#include <stdio.h>
#include <ttak/math/ntt.h>

int main(void) {
    const ttak_ntt_prime_t *prime = &ttak_ntt_primes[0];
    uint64_t data[4] = {1, 2, 3, 4};
    if (ttak_ntt_transform(data, 4, prime, false)) {
        printf("forward NTT: %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 "\n",
               data[0], data[1], data[2], data[3]);
        ttak_ntt_transform(data, 4, prime, true);
        printf("inverse NTT: %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 "\n",
               data[0], data[1], data[2], data[3]);
    }
    return 0;
}
