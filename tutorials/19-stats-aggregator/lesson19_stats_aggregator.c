#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <ttak/stats/stats.h>

int main(void) {
    ttak_stats_t stats;
    ttak_stats_init(&stats, 0, 100);
    for (uint64_t sample = 10; sample <= 50; sample += 10) {
        ttak_stats_record(&stats, sample);
    }
    printf("mean latency: %.2f\n", ttak_stats_mean(&stats));
    ttak_stats_print_ascii(&stats);
    return 0;
}
