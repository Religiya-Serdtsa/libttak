#define _XOPEN_SOURCE 700
#include <inttypes.h>
#include <stdio.h>
#include <ttak/atomic/atomic.h>

int main(void) {
    volatile uint64_t counter = 0;
    ttak_atomic_write64(&counter, 10);
    ttak_atomic_add64(&counter, 5);
    uint64_t snapshot = ttak_atomic_read64(&counter);
    printf("atomic counter: %" PRIu64 "\n", snapshot);
    return 0;
}
