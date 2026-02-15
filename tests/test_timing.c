#include <ttak/timing/timing.h>
#include <ttak/timing/deadline.h>
#include "test_macros.h"
#include <unistd.h>

static void test_timing_basic(void) {
    uint64_t t1 = ttak_get_tick_count();
    usleep(10000); // 10ms
    uint64_t t2 = ttak_get_tick_count();
    
    ASSERT(t1 > 0);
    ASSERT(t2 >= t1);
}

static void test_deadline_expiration(void) {
    ttak_deadline_t dl;
    ttak_deadline_set(&dl, 100);
    ASSERT(ttak_deadline_is_expired(&dl) == false);
    usleep(110000);
    ASSERT(ttak_deadline_is_expired(&dl) == true);
}

int main(void) {
    RUN_TEST(test_timing_basic);
    RUN_TEST(test_deadline_expiration);
    return 0;
}
