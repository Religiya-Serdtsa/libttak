#include <ttak/stats/system_usage.h>
#include <stdlib.h>
#include <string.h>
#include "test_macros.h"

static void test_system_usage_api_basics(void) {
    double cpu_total = ttak_get_cpu_usage_total();
    ASSERT(cpu_total >= -1.0);

    double cpu0 = ttak_get_cpu_usage_per_core(0);
    ASSERT(cpu0 >= -1.0);

    double gpu_total = ttak_get_gpu_usage_total();
    ASSERT(gpu_total >= -1.0);

    double gpu_cu0 = ttak_get_gpu_usage_per_cu(0);
    ASSERT(gpu_cu0 == -1.0 || gpu_cu0 >= 0.0);

    (void)ttak_get_rss_footprint();

    char *json = ttak_get_rss_footprint_full();
    ASSERT(json != NULL);
    ASSERT(strstr(json, "rss_bytes") != NULL);
    free(json);
}

int main(void) {
    RUN_TEST(test_system_usage_api_basics);
    return 0;
}
