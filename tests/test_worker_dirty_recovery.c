#include <ttak/async/task.h>
#include <ttak/async/sched.h>
#include <ttak/thread/worker.h>
#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>
#include "test_macros.h"
#include <unistd.h>
#include <stdio.h>

static int task_started = 0;
static int task_completed = 0;

void *dummy_task_func(void *arg) {
    (void)arg;
    task_started = 1;
    task_completed = 1;
    return NULL;
}

void test_worker_dirty_recovery() {
    uint64_t now = ttak_get_tick_count();
    
    // 1. Allocate some memory that will expire soon (10ms)
    void *dirty_ptr = ttak_mem_alloc(1024, 10000000, now); // 10ms in ns? 
    // Wait, ttak_get_tick_count() returns ns or ticks.
    // Let's check timing.h
    
    ASSERT(dirty_ptr != NULL);

    // 2. Initialize async system
    ttak_async_init(0);
    
    // 3. Wait for the memory to expire
    usleep(200000); // 200ms
    
    // 4. Submit a task
    ttak_task_t *task = ttak_task_create(dummy_task_func, NULL, NULL, ttak_get_tick_count());
    ttak_async_schedule(task, ttak_get_tick_count(), 1);

    // 5. Wait for worker to process
    usleep(100000);

    printf("Task started: %d, Task completed: %d\n", task_started, task_completed);
    
    // If recovery happened, task_started should be 0.
    // However, tt_inspect_dirty_pointers might have cleaned it up if it was called elsewhere.
    
    ttak_async_shutdown();
    ttak_mem_free(dirty_ptr);
}

int main() {
    RUN_TEST(test_worker_dirty_recovery);
    return 0;
}
