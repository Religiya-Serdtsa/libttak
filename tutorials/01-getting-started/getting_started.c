#define _XOPEN_SOURCE 700
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>

int main(void) {
    const uint64_t now = ttak_get_tick_count();
    const uint64_t lifetime = TT_MILLI_SECOND(0.3);

    printf("== LibTTAK getting started sample ==\n");
    printf("Requesting allocation at tick %" PRIu64 " with lifetime %" PRIu64 " ticks\n",
           now, lifetime);

    char *message = ttak_mem_alloc(128, lifetime, now);
    if (!message) {
        printf("LibTTAK fails to allocate a memory.\n");
        return 1;
    }

    snprintf(message, 128, "Hello from LibTTAK! lifetime=%" PRIu64 " ticks", lifetime);

    const uint64_t checkpoint = now + lifetime / 2;
    char *midway = ttak_mem_access(message, checkpoint);
    if (midway) {
        printf("[midway @ %" PRIu64 "] %s\n", checkpoint, midway);
    } else {
        printf("[midway @ %" PRIu64 "] Allocation expired earlier than expected\n", checkpoint);
    }

    const uint64_t expiry_probe = now + lifetime + 1;
    char *expired = ttak_mem_access(message, expiry_probe);
    if (!expired) {
        printf("[late @ %" PRIu64 "] Allocation expired as expected\n", expiry_probe);
    } else {
        printf("[late @ %" PRIu64 "] Unexpected access success, check your lifetime math: %s\n",
               expiry_probe, expired);
    }

    ttak_mem_free(message);
    puts("Allocation cleaned up. You're ready for Lesson 02!");
    return 0;
}
