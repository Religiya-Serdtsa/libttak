#ifndef TTAK_ASYNC_SCHED_H
#define TTAK_ASYNC_SCHED_H

#include <ttak/async/task.h>
#include <stdint.h>

void ttak_async_init(int nice);
void ttak_async_shutdown(void);
void ttak_async_schedule(ttak_task_t *task, uint64_t now, int priority);
void ttak_async_yield(void);

#endif // TTAK_ASYNC_SCHED_H
