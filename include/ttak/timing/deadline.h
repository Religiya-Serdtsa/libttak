#ifndef TTAK_TIMING_DEADLINE_H
#define TTAK_TIMING_DEADLINE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Represents a specific point in time used for deadline checks.
 */
typedef struct ttak_deadline {
    uint64_t start_ts;      /**< Timestamp when the deadline was set. */
    uint64_t deadline_ts;   /**< Target timestamp for expiration. */
} ttak_deadline_t;

typedef ttak_deadline_t tt_deadline_t;

/**
 * @brief Represents a duration used for timeouts.
 */
typedef struct ttak_timeout {
    uint64_t duration_ms;   /**< Duration in milliseconds. */
} ttak_timeout_t;

typedef ttak_timeout_t tt_timeout_t;

/**
 * @brief Sets a deadline relative to the current time.
 * 
 * @param dl Pointer to the deadline structure.
 * @param ms_from_now Milliseconds from now when the deadline expires.
 */
void ttak_deadline_set(ttak_deadline_t *dl, uint64_t ms_from_now);

/**
 * @brief Checks if the deadline has passed.
 * 
 * @param dl Pointer to the deadline structure.
 * @return true if expired, false otherwise.
 */
bool ttak_deadline_is_expired(const ttak_deadline_t *dl);

/**
 * @brief Returns the remaining time in milliseconds.
 * 
 * @param dl Pointer to the deadline structure.
 * @return Remaining milliseconds, or 0 if expired.
 */
uint64_t ttak_deadline_remaining(const ttak_deadline_t *dl);

#endif // TTAK_TIMING_DEADLINE_H