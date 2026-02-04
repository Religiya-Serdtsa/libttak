#include <stdio.h>
#include <ttak/async/sched.h>
#include <ttak/async/task.h>
#include <ttak/timing/timing.h>

static void *scheduled_task(void *arg) {
    puts((const char *)arg);
    return arg;
}

int main(void) {
    uint64_t now = ttak_get_tick_count();
    ttak_async_init(0);
    ttak_task_t *task = ttak_task_create(scheduled_task, (void *)"scheduler fired", NULL, now);
    ttak_async_schedule(task, now, 0);
    ttak_async_yield();
    ttak_async_shutdown();
    return 0;
}
