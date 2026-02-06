#define _XOPEN_SOURCE 700
#include <inttypes.h>
#include <stdio.h>
#include <ttak/stats/stats.h>
#include <ttak/timing/timing.h>

static void busy_work(void) {
    for (volatile int i = 0; i < 10000; ++i) {
    }
}

int main(void) {
    ttak_stats_t stats;
    ttak_stats_init(&stats, 0, 1000);
    const int runs = 5;
    for (int i = 0; i < runs; ++i) {
        uint64_t start = ttak_get_tick_count();
        busy_work();
        uint64_t end = ttak_get_tick_count();
        ttak_stats_record(&stats, end - start);
    }
    printf("bench runs recorded: %d\n", runs);
    ttak_stats_print_ascii(&stats);
    return 0;
}
