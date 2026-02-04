#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttak/mem/mem.h>

static uint64_t read_env_u64(const char *name, uint64_t fallback) {
    const char *value = getenv(name);
    if (!value || !*value) {
        return fallback;
    }

    char *end = NULL;
    uint64_t parsed = (uint64_t)strtoull(value, &end, 10);
    if (end && *end != '\0') {
        fprintf(stderr, "[warning] %s should be an integer, using fallback %" PRIu64 "\n",
                name, fallback);
        return fallback;
    }
    return parsed;
}

int main(void) {
    const uint64_t now = read_env_u64("NOW", 500);
    const uint64_t lifetime = read_env_u64("LIFETIME", 1200);

    printf("== LibTTAK getting started sample ==\n");
    printf("Requesting allocation at tick %" PRIu64 " with lifetime %" PRIu64 " ticks\n",
           now, lifetime);

    char *message = ttak_mem_alloc(128, lifetime, now);
    if (!message) {
        fputs("Allocation failed. Did you install libttak and link with -lttak?\n", stderr);
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
