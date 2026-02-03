#include <ttak/stats/stats.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/**
 * @brief Initializes stats with defined histogram range.
 */
void ttak_stats_init(ttak_stats_t *s, uint64_t hist_min, uint64_t hist_max) {
    s->count = 0;
    s->sum = 0;
    s->min = UINT64_MAX;
    s->max = 0;
    memset(s->histogram, 0, sizeof(s->histogram));
    s->hist_min = hist_min;
    s->hist_max = hist_max;
    // Calculate bucket size
    s->hist_step = (hist_max > hist_min) ? (hist_max - hist_min) / TTAK_STATS_HIST_BUCKETS : 1;
    if (s->hist_step == 0) s->hist_step = 1;
    ttak_spin_init(&s->lock);
}

/**
 * @brief Threads-safe recording of a value.
 */
void ttak_stats_record(ttak_stats_t *s, uint64_t value) {
    ttak_spin_lock(&s->lock);
    s->count++;
    s->sum += value;
    if (value < s->min) s->min = value;
    if (value > s->max) s->max = value;
    
    // Update histogram bucket
    if (value >= s->hist_min && value < s->hist_max) {
        size_t idx = (value - s->hist_min) / s->hist_step;
        if (idx >= TTAK_STATS_HIST_BUCKETS) idx = TTAK_STATS_HIST_BUCKETS - 1;
        s->histogram[idx]++;
    }
    ttak_spin_unlock(&s->lock);
}

/**
 * @brief Calculates mean.
 */
double ttak_stats_mean(ttak_stats_t *s) {
    ttak_spin_lock(&s->lock);
    double mean = (s->count > 0) ? (double)s->sum / s->count : 0.0;
    ttak_spin_unlock(&s->lock);
    return mean;
}

/**
 * @brief Prints ASCII representation of stats.
 */
void ttak_stats_print_ascii(ttak_stats_t *s) {
    ttak_spin_lock(&s->lock);
    printf("Stats: Count=%lu, Min=%lu, Max=%lu, Mean=%.2f\n", 
           s->count, s->min, s->max, (s->count > 0) ? (double)s->sum / s->count : 0.0);
    printf("Histogram:\n");
    
    // Find max count for scaling
    uint64_t max_count = 0;
    for (int i = 0; i < TTAK_STATS_HIST_BUCKETS; i++) {
        if (s->histogram[i] > max_count) max_count = s->histogram[i];
    }
    
    // Print bars
    for (int i = 0; i < TTAK_STATS_HIST_BUCKETS; i++) {
        uint64_t lower = s->hist_min + i * s->hist_step;
        uint64_t upper = lower + s->hist_step;
        printf("[%4lu-%4lu] ", lower, upper); 
        
        int bars = 0;
        if (max_count > 0) {
            bars = (s->histogram[i] * 20) / max_count;
        }
        
        for (int j = 0; j < bars; j++) printf("#");
        printf(" (%lu)\n", s->histogram[i]);
    }
    ttak_spin_unlock(&s->lock);
}