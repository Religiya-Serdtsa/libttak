#include <inttypes.h>
#include <stdio.h>
#include <ttak/thread/worker.h>
#include <ttak/timing/timing.h>

static void *demo_job(void *arg) {
    return arg;
}

int main(void) {
    ttak_worker_wrapper_t wrapper = {
        .func = demo_job,
        .arg = (void *)"worker payload",
        .promise = NULL,
        .ts = ttak_get_tick_count(),
        .nice_val = 0,
    };

    printf("wrapper prepared for worker @ %" PRIu64 "\n", wrapper.ts);
    puts("Lesson 13: shuttle wrappers between pool and worker safely.");
    return 0;
}
