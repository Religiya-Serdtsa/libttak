#ifndef TTAK_TIMING_H
#define TTAK_TIMING_H

#include <stdint.h>

/**
 * @brief Time unit macros for converting to nanoseconds.
 */
#define TT_NANO_SECOND(n)   ((uint64_t)(n))
#define TT_MICRO_SECOND(n)  ((uint64_t)(n) * 1000ULL)
#define TT_MILLI_SECOND(n)  ((uint64_t)(n) * 1000ULL * 1000ULL)
#define TT_SECOND(n)        ((uint64_t)(n) * 1000ULL * 1000ULL * 1000ULL)
#define TT_MINUTE(n)        ((uint64_t)(n) * 60ULL * 1000ULL * 1000ULL * 1000ULL)
#define TT_HOUR(n)          ((uint64_t)(n) * 60ULL * 60ULL * 1000ULL * 1000ULL * 1000ULL)

/**
 * @brief Returns the current tick count in milliseconds.
 * 
 * Used for unified lifecycle management across the library.
 * 
 * @return Current tick count (ms).
 */
uint64_t ttak_get_tick_count(void);

/**
 * @brief Returns the current tick count in nanoseconds.
 * 
 * Used for high-precision measurements.
 * 
 * @return Current tick count (ns).
 */
uint64_t ttak_get_tick_count_ns(void);

#endif // TTAK_TIMING_H
