#include <ttak/timing/deadline.h>
#include <ttak/timing/timing.h>

/**
 * @brief Calculates and stores deadline timestamps.
 */
void ttak_deadline_set(ttak_deadline_t *dl, uint64_t ms_from_now) {
    dl->start_ts = ttak_get_tick_count();
    dl->deadline_ts = dl->start_ts + ms_from_now;
}

/**
 * @brief Compares current tick count with deadline.
 */
bool ttak_deadline_is_expired(const ttak_deadline_t *dl) {
    return ttak_get_tick_count() >= dl->deadline_ts;
}

/**
 * @brief Calculates remaining time safe against underflow.
 */
uint64_t ttak_deadline_remaining(const ttak_deadline_t *dl) {
    uint64_t now = ttak_get_tick_count();
    if (now >= dl->deadline_ts) return 0;
    return dl->deadline_ts - now;
}