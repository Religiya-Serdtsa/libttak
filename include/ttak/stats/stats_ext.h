#ifndef TTAK_STATS_STATS_EXT_H
#define TTAK_STATS_STATS_EXT_H

#include <ttak/stats/stats.h>
#include <ttak/math/bigint.h>
#include <ttak/math/bigreal.h>
#include <ttak/priority/scheduler.h>

/**
 * @brief Extended statistics structure with automatic promotion and high precision.
 */
typedef struct ttak_stats_ext {
    ttak_stats_t base;
    _Bool promoted;
    
    // High precision accumulators (used if promoted or for complex stats)
    ttak_bigint_t count_big;
    ttak_bigint_t sum_x_big;
    ttak_bigint_t sum_x_sq_big;
    ttak_bigint_t sum_y_big;
    ttak_bigint_t sum_y_sq_big;
    ttak_bigint_t sum_xy_big;

    ttak_spin_t lock_ext;
} ttak_stats_ext_t;

/**
 * @brief Initializes the extended statistics.
 */
void ttak_stats_ext_init(ttak_stats_ext_t *s, uint64_t now);

/**
 * @brief Records a sample pair (x, y) for correlation/regression.
 * If y is not needed, pass 0.
 */
void ttak_stats_ext_record(ttak_stats_ext_t *s, uint64_t x, uint64_t y, uint64_t now);

/**
 * @brief Calculates the variance.
 */
_Bool ttak_stats_ext_variance(ttak_bigreal_t *res, ttak_stats_ext_t *s, uint64_t now);

/**
 * @brief Calculates the standard deviation.
 */
_Bool ttak_stats_ext_stddev(ttak_bigreal_t *res, ttak_stats_ext_t *s, uint64_t now);

/**
 * @brief Calculates the correlation coefficient.
 */
_Bool ttak_stats_ext_correlation(ttak_bigreal_t *res, ttak_stats_ext_t *s, uint64_t now);

/**
 * @brief Performs linear regression, returning slope and intercept.
 */
_Bool ttak_stats_ext_linear_regression(ttak_bigreal_t *slope, ttak_bigreal_t *intercept, ttak_stats_ext_t *s, uint64_t now);

/**
 * @brief Distribution analysis: Normal PDF.
 */
_Bool ttak_stats_dist_normal(ttak_bigreal_t *res, const ttak_bigreal_t *x, const ttak_bigreal_t *mu, const ttak_bigreal_t *sigma, uint64_t now);

/**
 * @brief Distribution analysis: Poisson PDF.
 */
_Bool ttak_stats_dist_poisson(ttak_bigreal_t *res, uint64_t k, const ttak_bigreal_t *lambda, uint64_t now);

/**
 * @brief Distribution analysis: Binomial PDF.
 */
_Bool ttak_stats_dist_binomial(ttak_bigreal_t *res, uint64_t k, uint64_t n, const ttak_bigreal_t *p, uint64_t now);

/**
 * @brief Parallel mean/variance calculation for large datasets.
 */
_Bool ttak_stats_parallel_process(ttak_stats_ext_t *s, uint64_t *data, size_t count, ttak_scheduler_t *sched, uint64_t now);

/**
 * @brief Calculates specific percentiles (P50, P95, P99, P99.9) from a dataset.
 * 
 * Note: This operation modifies the input 'data' array by sorting it.
 * 
 * @param data Array of samples.
 * @param count Number of samples.
 * @param p50 Result for the 50th percentile (Median).
 * @param p95 Result for the 95th percentile.
 * @param p99 Result for the 99th percentile.
 * @param p999 Result for the 99.9th percentile.
 * @param now Timestamp for result initialization.
 * @return true if successful.
 */
_Bool ttak_stats_compute_percentiles(uint64_t *data, size_t count,
                                     ttak_bigreal_t *p50, ttak_bigreal_t *p95,
                                     ttak_bigreal_t *p99, ttak_bigreal_t *p999,
                                     uint64_t now);

#endif // TTAK_STATS_STATS_EXT_H
