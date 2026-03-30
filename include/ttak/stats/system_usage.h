#ifndef TTAK_STATS_SYSTEM_USAGE_H
#define TTAK_STATS_SYSTEM_USAGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

double ttak_get_cpu_usage_total(void);
double ttak_get_cpu_usage_per_core(int core);
double ttak_get_gpu_usage_total(void);
double ttak_get_gpu_usage_per_cu(int cu);
size_t ttak_get_rss_footprint(void);
char *ttak_get_rss_footprint_full(void);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_STATS_SYSTEM_USAGE_H */
