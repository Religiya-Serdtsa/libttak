#include <inttypes.h>
#include <stdio.h>
#include <ttak/timing/deadline.h>
#include <ttak/timing/timing.h>

int main(void) {
    uint64_t tick_ms = ttak_get_tick_count();
    printf("tick(ms) = %" PRIu64 "\n", tick_ms);
    ttak_deadline_t dl;
    ttak_deadline_set(&dl, 50);
    printf("deadline remaining ~%" PRIu64 " ms\n", ttak_deadline_remaining(&dl));
    return 0;
}
