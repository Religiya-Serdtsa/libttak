#include <ttak/async/future.h>
#include <ttak/mem/epoch.h>
#include <ttak/timing/timing.h>
#include <stddef.h>
#if defined(EMBEDDED_BAREMETAL)
#include <ttak/async/sched.h>
#endif

/**
 * @brief Retrieve the computed result from the future.
 * * Synchronously blocks the calling thread until the producer signals completion.
 * To prevent epoch stalls and potential deadlocks with the background GC thread,
 * this function temporarily exits the epoch critical section during the wait period.
 *
 * @param future Pointer to the future object.
 * @return void* The result pointer assigned by the task, or NULL if invalid.
 */
void *ttak_future_get(ttak_future_t *future) {
    if (!future) {
        return NULL;
    }

    pthread_mutex_lock(&future->mutex);

    /* * Transition the current thread to an inactive state before blocking.
     * This allows the background GC (Epoch Manager) to advance the global epoch
     * without being stalled by this waiting thread.
     */
    ttak_epoch_exit();

    while (!future->ready) {
#if defined(EMBEDDED_BAREMETAL)
        /* On bare-metal there are no worker threads.  Drive the cooperative
         * scheduler so that the task which will fulfill this future can run. */
        pthread_mutex_unlock(&future->mutex);
        ttak_cooperative_run_once(ttak_get_tick_count());
        pthread_mutex_lock(&future->mutex);
#else
        /* Suspend execution until the future state is set to ready by the worker. */
        pthread_cond_wait(&future->cond, &future->mutex);
#endif
    }

    /* * Re-enter the epoch critical section upon wakeup to ensure that
     * any subsequent memory access to the returned result remains safe.
     */
    ttak_epoch_enter();

    void *res = future->result;
    pthread_mutex_unlock(&future->mutex);

    return res;
}
