#ifndef TTAK_STATS_STATS_H
#define TTAK_STATS_STATS_H

#include <stdint.h>
#include <ttak/sync/sync.h>
#include <ttak/sync/spinlock.h>

#define TTAK_STATS_HIST_BUCKETS 10

/**
 * @brief Thread-safe Statistics structure.
 * 
 * Tracks count, sum, min, max, and a simple histogram.
 */
typedef struct ttak_stats {
    uint64_t count;                             /**< Total number of samples. */
    uint64_t sum;                               /**< Sum of all sample values. */
    uint64_t min;                               /**< Minimum value observed. */
    uint64_t max;                               /**< Maximum value observed. */
    uint64_t histogram[TTAK_STATS_HIST_BUCKETS];/**< Histogram buckets. */
    uint64_t hist_min;                          /**< Lower bound for the histogram. */
    uint64_t hist_max;                          /**< Upper bound for the histogram. */
    uint64_t hist_step;                         /**< Range of each histogram bucket. */
    ttak_spin_t lock;                           /**< Spinlock for thread safety. */
} ttak_stats_t;

typedef ttak_stats_t tt_stats_t;

/**
 * @brief Initializes the statistics structure.
 * 
 * @param s Pointer to the stats structure.
 * @param hist_min Lower bound for the histogram.
 * @param hist_max Upper bound for the histogram.
 */
void ttak_stats_init(ttak_stats_t *s, uint64_t hist_min, uint64_t hist_max);

/**
 * @brief Records a new value into the statistics.
 * 
 * @param s Pointer to the stats structure.
 * @param value The value to record.
 */
void ttak_stats_record(ttak_stats_t *s, uint64_t value);

/**
 * @brief Prints the statistics to stdout in ASCII format.
 * 
 * Includes basic metrics and a bar chart for the histogram.
 * 
 * @param s Pointer to the stats structure.
 */
void ttak_stats_print_ascii(ttak_stats_t *s);

/**
 * @brief Calculates the arithmetic mean.
 * 
 * @param s Pointer to the stats structure.
 * @return The mean value.
 */
double ttak_stats_mean(ttak_stats_t *s);

#include <ttak/math/bigreal.h>

/**
 * @brief Computes percentiles from raw data and outputs them as bigreal numbers.
 */
void ttak_stats_compute_percentiles(uint64_t *data, size_t count, 
                                   ttak_bigreal_t *p50, ttak_bigreal_t *p95, 
                                   ttak_bigreal_t *p99, ttak_bigreal_t *p999, 
                                   uint64_t now);

#endif // TTAK_STATS_STATS_H