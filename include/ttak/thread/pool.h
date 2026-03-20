/**
 * @file pool.h
 * @brief Managed thread pool built on top of the async task scheduler.
 *
 * Provides a fixed-size pool of worker threads that drain a sharded task
 * queue.  Tasks are routed to shards via a deterministic hash-to-coordinate
 * mapping backed by a Latin-square routing table, reducing lock contention
 * by confining queue operations to a single shard lock rather than a global
 * pool lock.  Threads are created lazily and respect the nice value set at
 * init time.
 */

#ifndef TTAK_THREAD_POOL_H
#define TTAK_THREAD_POOL_H

#include <stddef.h>
#include <stdint.h>
#include <ttak/async/task.h>
#include <ttak/thread/worker.h>
#include <ttak/priority/queue.h>

/** Number of independent queue shards in the pool. Must equal TTAK_POOL_SHARD_COUNT. */
#define TTAK_THREAD_POOL_SHARDS 8

typedef struct ttak_thread_pool ttak_thread_pool_t;

typedef struct ttak_worker ttak_worker_t;

/**
 * @brief One independent queue shard with its own lock and condition variable.
 *
 * Confining push/pop operations to a single shard reduces the probability
 * that two concurrent enqueue/dequeue calls contend on the same mutex.
 */
typedef struct {
    __i_tt_proc_pq_t  queue;  /**< Shard-local priority queue.          */
    pthread_mutex_t   lock;   /**< Mutex protecting this shard's queue.  */
    pthread_cond_t    cond;   /**< Condition variable for this shard.    */
} ttak_pool_shard_t;

struct ttak_thread_pool {
    size_t              num_threads;
    ttak_worker_t       **workers;

    /** Sharded queues — tasks are routed here via the deterministic mapping. */
    ttak_pool_shard_t   shards[TTAK_THREAD_POOL_SHARDS];

    /** Coarse-grained lock used only during shutdown signalling. */
    pthread_mutex_t     pool_lock;
    /** Condition variable broadcast on shutdown so all workers wake. */
    pthread_cond_t      task_cond;
    uint64_t            creation_ts;
    _Bool               is_shutdown;

    /**
     * @brief Kills all sub-threads.
     */
    void (*force_shutdown)(ttak_thread_pool_t *pool);
};

ttak_thread_pool_t *ttak_thread_pool_create(size_t num_threads, int default_nice, uint64_t now);
void ttak_thread_pool_destroy(ttak_thread_pool_t *pool);
ttak_future_t *ttak_thread_pool_submit_task(ttak_thread_pool_t *pool, void *(*func)(void *), void *arg, int priority, uint64_t now);
_Bool ttak_thread_pool_schedule_task(ttak_thread_pool_t *pool, ttak_task_t *task, int priority, uint64_t now);

extern ttak_thread_pool_t *async_pool;

#endif // TTAK_THREAD_POOL_H
