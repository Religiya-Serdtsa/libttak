#include <ttak/timing/timing.h>
#include <time.h>
#ifdef _WIN32
    #include <windows.h>
    #include <stdbool.h>
#endif

/**
 * @brief Return a monotonic tick count in nanoseconds.
 *
 * @return Current CLOCK_MONOTONIC reading in milliseconds.
 */
uint64_t ttak_get_tick_count(void) {
#ifdef _WIN32
    return ttak_get_tick_count_ns() / 1000000ULL;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
#endif
}

/**
 * @brief Return a high-precision tick count in nanoseconds.
 *
 * @return Current CLOCK_MONOTONIC in nanoseconds.
 */
uint64_t ttak_get_tick_count_ns(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static bool init = false;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = true;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000LL) / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
#endif
}
