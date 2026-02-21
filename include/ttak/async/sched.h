#ifndef TTAK_ASYNC_SCHED_H
#define TTAK_ASYNC_SCHED_H

#include <ttak/async/task.h>
#include <stdint.h>
#ifndef _WIN32
#include <sched.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

void ttak_async_init(int nice);
void ttak_async_shutdown(void);
void ttak_async_schedule(ttak_task_t *task, uint64_t now, int priority);
static inline void ttak_async_yield(void) {
#ifndef _WIN32
    sched_yield();
#else
    SwitchToThread();
#endif
}

#endif // TTAK_ASYNC_SCHED_H
