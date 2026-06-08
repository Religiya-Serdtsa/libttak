/**
 * @file sched.h
 * @brief Async task scheduler — init, shutdown, schedule, and yield.
 *
 * Manages the global worker-thread pool.  Tasks submitted via
 * ttak_async_schedule() are picked up by threads in priority order.
 * ttak_async_yield() relinquishes the CPU slice without sleeping.
 */

#ifndef TTAK_ASYNC_SCHED_H
#define TTAK_ASYNC_SCHED_H

#include <ttak/async/task.h>
#include <stdint.h>
#include <stdbool.h>
#if !defined(_WIN32) && !defined(EMBEDDED_BAREMETAL)
#include <sched.h>
#elif defined(EMBEDDED_BAREMETAL)
/* bare-metal: no sched_yield */
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

/**
 * @brief Initialises the global async thread pool.
 *
 * @param nice Base nice value applied to worker threads (0 = normal).
 */
void ttak_async_init(int nice);

/** @brief Signals all workers to drain their queues and exit. */
void ttak_async_shutdown(void);

/**
 * @brief Submits a task for asynchronous execution.
 *
 * @param task     Task to enqueue.
 * @param now      Current monotonic timestamp in nanoseconds.
 * @param priority Scheduling priority (lower = higher priority).
 */
void ttak_async_schedule(ttak_task_t *task, uint64_t now, int priority);

/** @brief Yields the current CPU time slice without sleeping. */
static inline void ttak_async_yield(void) {
#if !defined(_WIN32) && !defined(EMBEDDED_BAREMETAL)
    sched_yield();
#elif defined(_WIN32)
    SwitchToThread();
#else
    /* bare-metal: cooperative loop, no yield needed */
#endif
}

#if defined(EMBEDDED_BAREMETAL)
/** @brief Initialise the cooperative run-queue (bare-metal only). */
void ttak_cooperative_init(void);

/** @brief Drain and execute one task from the cooperative queue.
 *
 *  Call this from the main loop to drive background work.
 *
 *  @param now Current monotonic timestamp.
 *  @return true if a task was executed, false if the queue was empty.
 */
bool ttak_cooperative_run_once(uint64_t now);

/** @brief Destroy the cooperative run-queue and free any pending tasks. */
void ttak_cooperative_shutdown(void);
#endif /* EMBEDDED_BAREMETAL */

#endif // TTAK_ASYNC_SCHED_H
