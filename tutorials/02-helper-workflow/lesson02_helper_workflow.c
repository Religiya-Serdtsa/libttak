#include <inttypes.h>
#include <stdio.h>
#include <ttak/timing/timing.h>

static void log_checkpoint(const char *step, uint64_t start_ms) {
    uint64_t now = ttak_get_tick_count();
    printf("[helper] %-20s +%" PRIu64 " ms\n", step, now - start_ms);
}

int main(void) {
    const uint64_t boot_ms = ttak_get_tick_count();
    log_checkpoint("build helper", boot_ms);
    log_checkpoint("page manual", boot_ms);
    const uint64_t hi_res = ttak_get_tick_count_ns();
    printf("[helper] hi-res tick snapshot: %" PRIu64 " ns\n", hi_res);
    log_checkpoint("mark shortcuts", boot_ms);
    puts("Lesson 02 ready: capture the commands you ran.");
    return 0;
}
