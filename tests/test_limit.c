#include <ttak/limit/limit.h>
#include "test_macros.h"
#include <unistd.h>

static void test_ratelimit_refill_logic(void) {
    ttak_ratelimit_t rl;
    ttak_ratelimit_init(&rl, 10.0, 2.0); /* 10 tokens/sec, burst 2 */

    ASSERT(ttak_ratelimit_allow(&rl));
    ASSERT(ttak_ratelimit_allow(&rl));
    ASSERT(ttak_ratelimit_allow(&rl) == false);

    usleep(110000); /* 110ms -> ~1 token */
    ASSERT(ttak_ratelimit_allow(&rl));
}

int main(void) {
    RUN_TEST(test_ratelimit_refill_logic);
    return 0;
}
