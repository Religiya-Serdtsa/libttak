#include <ttak/priority/nice.h>

/**
 * @brief Clamp a POSIX nice value into the scheduler priority range.
 *
 * @param nice Requested nice value.
 * @return Clamped scheduler priority.
 */
int ttak_nice_to_prio(int nice) {
    if (nice < __TT_PRIO_MIN) return __TT_PRIO_MIN;
    if (nice > __TT_PRIO_MAX) return __TT_PRIO_MAX;
    return nice;
}

/**
 * @brief Compare two nice values.
 *
 * @param nice1 First nice value.
 * @param nice2 Second nice value.
 * @return Positive if nice1 > nice2, negative if less, zero if equal.
 */
int ttak_compare_nice(int nice1, int nice2) {
    return nice1 - nice2;
}

#include <stdlib.h>

/**
 * @brief Randomly shuffle an array of nice values.
 *
 * @param nices Array to permute.
 * @param count Number of elements.
 */
void ttak_shuffle_by_nice(int *nices, size_t count) {
    if (!nices || count <= 1) return;
    for (size_t i = 0; i < count - 1; i++) {
        size_t j = i + rand() / (RAND_MAX / (count - i) + 1);
        int t = nices[j];
        nices[j] = nices[i];
        nices[i] = t;
    }
}

/**
 * @brief Clamp the priority to the allowed lockable range.
 *
 * @param nice Requested nice level.
 * @return Adjusted priority suitable for locking semantics.
 */
int ttak_lock_priority(int nice) {
    if (nice < __TT_SCHED_NORMAL__) return __TT_SCHED_NORMAL__;
    if (nice > __TT_PRIO_MAX) return __TT_PRIO_MAX;
    return nice;
}
