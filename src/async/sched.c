/**
 * @file sched.c
 * @brief Async scheduler implementation — thread pool init, submit, drain.
 *
 * Initialises a fixed-size pool of POSIX threads (or Windows threads) and
 * feeds them tasks from a priority queue.  Uses ttak_nice_to_prio() to map
 * the caller's nice value to a queue index.
 */

#include <ttak/async/sched.h>
#include <ttak/thread/pool.h>
#include <ttak/timing/timing.h>
#include <ttak/mem/epoch.h>
#ifndef _WIN32
#include <sched.h>
#endif
#include <stddef.h>
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#  if defined(__NetBSD__) || defined(__OpenBSD__)
#    include "../../internal/compat/bsd_sysctl.h"
#  endif
#endif

ttak_thread_pool_t *async_pool = NULL;

void ttak_async_init(int nice) {
    long available_cores;
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    available_cores = sysinfo.dwNumberOfProcessors;
#elif defined(__NetBSD__) || defined(__OpenBSD__)
    int mib[2] = { CTL_HW, HW_NCPU };
    int ncpu = 1;
    size_t len = sizeof(ncpu);
    if (sysctl(mib, 2, &ncpu, &len, NULL, 0) == -1 || ncpu < 1) ncpu = 1;
    available_cores = ncpu;
#else
    available_cores = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    size_t target_threads = (available_cores > 0) ? (size_t)available_cores / 4 : 0;
    if (target_threads == 0) target_threads = 1;
    uint64_t now = ttak_get_tick_count();
    if (async_pool) ttak_thread_pool_destroy(async_pool);
    async_pool = ttak_thread_pool_create(target_threads, nice, now);
}

void ttak_async_shutdown(void) {
    if (!async_pool) return;
    ttak_thread_pool_destroy(async_pool);
    async_pool = NULL;
}

void ttak_async_schedule(ttak_task_t *task, uint64_t now, int priority) {
    if (!task) return;
    if (async_pool) {
        ttak_task_t *queued_task = ttak_task_clone(task, now);
        if (queued_task) {
            if (ttak_thread_pool_schedule_task(async_pool, queued_task, priority, now)) return;
            ttak_task_destroy(queued_task, now);
        }
    }
    ttak_epoch_enter();
    ttak_task_execute(task, now);
    ttak_epoch_exit();
}
