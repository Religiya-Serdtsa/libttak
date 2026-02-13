#include <ttak/async/task.h>
#include <ttak/async/sched.h>
#include <ttak/thread/worker.h>
#include <ttak/mem/mem.h>
#include "test_macros.h"
#include <unistd.h>

static int abort_executed = 0;
static int post_abort_executed = 0;

void *task_abort_func(void *arg) {
    (void)arg;
    abort_executed = 1;
    ttak_worker_abort();
    post_abort_executed = 1;
    return NULL;
}

void test_worker_abort_recovery() {
    uint64_t now = 5000;
    
    ttak_async_init(0); // Initialize with default pool
    
    ttak_task_t *task = ttak_task_create(task_abort_func, NULL, NULL, now);
    ASSERT(task != NULL);

    // Schedule task on the pool
    ttak_async_schedule(task, now + 10, 1);
    
    // Wait for task to be processed
    int retry = 0;
    while (!abort_executed && retry < 100) {
        usleep(10000); // 10ms
        retry++;
    }

    ASSERT(abort_executed == 1);
    ASSERT(post_abort_executed == 0); // Should have longjmp'd before this
    
    ttak_async_shutdown();
}

int main() {
    RUN_TEST(test_worker_abort_recovery);
    return 0;
}
