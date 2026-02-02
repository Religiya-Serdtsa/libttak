#include <ttak/timing/timing.h>
#include <time.h>

/**
 * @brief Return a monotonic tick count in milliseconds.
 *
 * @return Current CLOCK_MONOTONIC reading.
 */
uint64_t ttak_get_tick_count(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
