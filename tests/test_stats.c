#include <ttak/stats/stats.h>
#include "test_macros.h"

static void test_stats_accumulates_basic_metrics(void) {
    ttak_stats_t stats;
    ttak_stats_init(&stats, 0, 100);

    ttak_stats_record(&stats, 10);
    ttak_stats_record(&stats, 20);
    ttak_stats_record(&stats, 30);

    ASSERT(stats.count == 3);
    ASSERT(stats.min == 10);
    ASSERT(stats.max == 30);
    ASSERT(ttak_stats_mean(&stats) == 20.0);
}

int main(void) {
    RUN_TEST(test_stats_accumulates_basic_metrics);
    return 0;
}
