/**
 * @file sched.c
 * @brief Async scheduler implementation — thread pool init, submit, drain.
 *
 * Initialises a fixed-size pool of POSIX threads (or Windows threads) and
 * feeds them tasks from a priority queue.  Uses ttak_nice_to_prio() to map
 * the caller's nice value to a queue index.
 *
 * On EMBEDDED_BAREMETAL targets the thread pool is replaced by a single
 * cooperative run-queue.  Tasks are enqueued by ttak_async_schedule() and
 * executed later by ttak_cooperative_run_once() called from the main loop.
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

#if defined(EMBEDDED_BAREMETAL)
#include <ttak/priority/simple.h>
#endif

ttak_thread_pool_t *async_pool = NULL;

#if defined(EMBEDDED_BAREMETAL)
static ttak_simple_queue_t g_coop_queue;
static bool g_coop_init = false;
#endif

void ttak_async_init(int nice) {
#if !defined(EMBEDDED_BAREMETAL)
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
#else
    (void)nice;
    ttak_cooperative_init();
#endif
}

void ttak_async_shutdown(void) {
#if !defined(EMBEDDED_BAREMETAL)
    if (!async_pool) return;
    ttak_thread_pool_destroy(async_pool);
    async_pool = NULL;
#else
    ttak_cooperative_shutdown();
#endif
}

void ttak_async_schedule(ttak_task_t *task, uint64_t now, int priority) {
    if (!task) return;
#if !defined(EMBEDDED_BAREMETAL)
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
#else
    (void)priority;
    ttak_task_t *queued_task = ttak_task_clone(task, now);
    if (queued_task) {
        ttak_simple_queue_push(&g_coop_queue, queued_task, now);
    } else {
        /* If cloning fails, execute synchronously. */
        ttak_epoch_enter();
        ttak_task_execute(task, now);
        ttak_epoch_exit();
    }
#endif
}

#if defined(EMBEDDED_BAREMETAL)
void ttak_cooperative_init(void) {
    ttak_simple_queue_init(&g_coop_queue);
    g_coop_init = true;
}

bool ttak_cooperative_run_once(uint64_t now) {
    if (!g_coop_init) return false;
    ttak_task_t *task = (ttak_task_t *)ttak_simple_queue_pop(&g_coop_queue, now);
    if (!task) return false;
    ttak_epoch_enter();
    ttak_task_execute(task, now);
    ttak_epoch_exit();
    ttak_task_destroy(task, now);
    return true;
}

void ttak_cooperative_shutdown(void) {
    if (!g_coop_init) return;
    uint64_t now = ttak_get_tick_count();
    ttak_task_t *task;
    while ((task = (ttak_task_t *)ttak_simple_queue_pop(&g_coop_queue, now)) != NULL) {
        ttak_task_destroy(task, now);
    }
    g_coop_init = false;
}
#endif /* EMBEDDED_BAREMETAL */
