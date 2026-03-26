#include <ttak/thread/pool.h>
#include <ttak/mem/mem.h>
#include <ttak/sync/sync.h>
#include <ttak/async/promise.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include <ttak/priority/scheduler.h>
#include "../../internal/ttak/shard_map.h"

/**
 * @brief Stop all workers and signal shutdown.
 *
 * Acquires the coarse pool_lock (used only here and for is_shutdown checks)
 * to flip the flag, then broadcasts on every shard condition variable so that
 * workers blocked in their preferred shard's cond_wait wake up immediately.
 *
 * @param pool Pool to shut down.
 */
static void pool_force_shutdown(ttak_thread_pool_t *pool) {
    if (!pool) return;
    pthread_mutex_lock(&pool->pool_lock);
    pool->is_shutdown = true;
    for (size_t i = 0; i < pool->num_threads; i++) {
        if (pool->workers[i]) {
            pool->workers[i]->should_stop = true;
        }
    }
    /* Wake workers waiting on any shard cond */
    for (size_t s = 0; s < TTAK_THREAD_POOL_SHARDS; s++) {
        pthread_cond_broadcast(&pool->shards[s].cond);
    }
    pthread_cond_broadcast(&pool->task_cond);
    pthread_mutex_unlock(&pool->pool_lock);
}

/**
 * @brief Create a thread pool with the given worker count.
 *
 * Initialises TTAK_THREAD_POOL_SHARDS independent queue shards, each with its
 * own mutex and condition variable, then spawns @p num_threads workers.  Each
 * worker is assigned a preferred shard via ttak_shard_for_worker().
 *
 * @param num_threads Number of worker threads.
 * @param default_nice Initial nice value for workers.
 * @param now         Timestamp for memory tracking.
 * @return Pointer to the created pool or NULL on failure.
 */
ttak_thread_pool_t *ttak_thread_pool_create(size_t num_threads, int default_nice, uint64_t now) {
    /* Initialize pthread attribute if available so Windows shim can shrink stacks */
    pthread_attr_t attr;
    const pthread_attr_t *attr_for_threads = NULL;
    if (pthread_attr_init(&attr) == 0) {
        pthread_attr_setstacksize(&attr, 1024 * 512);
        attr_for_threads = &attr;
    }

    /* Ensure smart scheduler is ready */
    ttak_scheduler_init();

    ttak_thread_pool_t *pool = (ttak_thread_pool_t *)ttak_mem_alloc(sizeof(ttak_thread_pool_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!pool) {
        if (attr_for_threads) pthread_attr_destroy(&attr);
        return NULL;
    }

    pool->num_threads = num_threads;
    pool->creation_ts = now;
    pool->is_shutdown = false;
    pool->force_shutdown = pool_force_shutdown;

    /* Coarse lock used only for shutdown signalling */
    pthread_mutex_init(&pool->pool_lock, NULL);
    pthread_cond_init(&pool->task_cond, NULL);

    /* Initialise all per-shard queues, locks, and condition variables */
    for (size_t s = 0; s < TTAK_THREAD_POOL_SHARDS; s++) {
        ttak_priority_queue_init(&pool->shards[s].queue);
        pthread_mutex_init(&pool->shards[s].lock, NULL);
        pthread_cond_init(&pool->shards[s].cond, NULL);
    }

    pool->workers = (ttak_worker_t **)ttak_mem_alloc(sizeof(ttak_worker_t *) * num_threads, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!pool->workers) {
        fprintf(stderr, "[FATAL] Failed to allocate worker array\n");
        for (size_t s = 0; s < TTAK_THREAD_POOL_SHARDS; s++) {
            pthread_mutex_destroy(&pool->shards[s].lock);
            pthread_cond_destroy(&pool->shards[s].cond);
        }
        pthread_mutex_destroy(&pool->pool_lock);
        pthread_cond_destroy(&pool->task_cond);
        if (attr_for_threads) pthread_attr_destroy(&attr);
        ttak_mem_free(pool);
        return NULL;
    }

    for (size_t i = 0; i < num_threads; i++) {
        pool->workers[i] = (ttak_worker_t *)ttak_mem_alloc(sizeof(ttak_worker_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
        if (!pool->workers[i]) {
            fprintf(stderr, "[FATAL] Failed to allocate worker %zu\n", i);
            pool->num_threads = i;
            pool_force_shutdown(pool);
            if (attr_for_threads) pthread_attr_destroy(&attr);
            return NULL;
        }
        pool->workers[i]->pool = pool;
        pool->workers[i]->should_stop = false;
        pool->workers[i]->exit_code = 0;
        /* Assign shard affinity: spread workers evenly across shards */
        pool->workers[i]->preferred_shard = ttak_shard_for_worker(i);

        pool->workers[i]->wrapper = (ttak_worker_wrapper_t *)ttak_mem_alloc(sizeof(ttak_worker_wrapper_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
        if (!pool->workers[i]->wrapper) {
            fprintf(stderr, "[FATAL] Failed to allocate worker wrapper %zu\n", i);
            ttak_mem_free(pool->workers[i]);
            pool->workers[i] = NULL;
            pool->num_threads = i;
            pool_force_shutdown(pool);
            if (attr_for_threads) pthread_attr_destroy(&attr);
            return NULL;
        }
        pool->workers[i]->wrapper->nice_val = default_nice;
        pool->workers[i]->wrapper->ts = now;
        /* Pre-initialize jump magic so abort checks don't fail before first setjmp */
        pool->workers[i]->wrapper->jmp_magic = 0;
        pool->workers[i]->wrapper->jmp_tid = 0;

        int rc = pthread_create(&pool->workers[i]->thread, attr_for_threads, ttak_worker_routine, pool->workers[i]);
        if (rc != 0) {
            fprintf(stderr, "[FATAL] Failed to create worker thread %zu: %d\n", i, rc);
            pool->num_threads = i;
            pool_force_shutdown(pool);
            if (attr_for_threads) pthread_attr_destroy(&attr);
            return NULL;
        }
    }

    if (attr_for_threads) pthread_attr_destroy(&attr);
    return pool;
}

/**
 * @brief Submit a function to be executed asynchronously.
 *
 * @param pool     Pool receiving the work.
 * @param func     Function pointer to execute.
 * @param arg      Argument passed to the function.
 * @param priority Scheduling priority hint.
 * @param now      Timestamp for memory bookkeeping.
 * @return Future representing the eventual result, or NULL on failure.
 */
ttak_future_t *ttak_thread_pool_submit_task(ttak_thread_pool_t *pool, void *(*func)(void *), void *arg, int priority, uint64_t now) {
    if (!pool) return NULL;

    ttak_promise_t *promise = ttak_promise_create(now);
    if (!promise) return NULL;

    ttak_task_t *task = ttak_task_create((ttak_task_func_t)func, arg, promise, now);
    if (!task) {
        ttak_mem_free(promise->future);
        ttak_mem_free(promise); /* destroys promise here. */
        return NULL; 
    }

    /* Apply smart scheduling adjustment */
    int adjusted_priority = ttak_scheduler_get_adjusted_priority(task, priority);

    if (!ttak_thread_pool_schedule_task(pool, task, adjusted_priority, now)) {
        ttak_task_destroy(task, now);
        ttak_mem_free(promise->future);
        ttak_mem_free(promise);
        return NULL;
    }

    return ttak_promise_get_future(promise);
}

/**
 * @brief Queue a prepared task for execution using deterministic shard routing.
 *
 * The task's hash is mapped to (row, col) coordinates via Fibonacci hashing,
 * then indexed into the Latin-square routing table to select a shard.  Only
 * the selected shard's lock is held during the push, keeping the critical
 * section small.
 *
 * When the task hash is 0 (unknown), the task falls back to shard 0.
 *
 * @param pool     Pool to enqueue into.
 * @param task     Task instance created earlier.
 * @param priority Priority hint.
 * @param now      Timestamp for queue bookkeeping.
 * @return true if scheduled, false if the pool is shutting down.
 */
_Bool ttak_thread_pool_schedule_task(ttak_thread_pool_t *pool, ttak_task_t *task, int priority, uint64_t now) {
    if (!pool || !task) return 0;

    /* Abort early without touching any lock */
    if (pool->is_shutdown) return 0;

    /* Deterministic shard selection via hash → coordinate → table lookup */
    uint64_t hash = ttak_task_get_hash(task);
    size_t shard_idx = (hash != 0) ? ttak_shard_for_hash(hash) : 0;
    ttak_pool_shard_t *shard = &pool->shards[shard_idx];

    pthread_mutex_lock(&shard->lock);
    if (pool->is_shutdown) {
        pthread_mutex_unlock(&shard->lock);
        return 0;
    }

    shard->queue.push(&shard->queue, task, priority, now);
    
    /* 
     * Wake up affinity-bound workers on this shard.
     * Also signal other shards so that idle workers there can perform work stealing.
     * This prevents starvation when long-running tasks are unevenly distributed.
     */
    for (size_t s = 0; s < TTAK_THREAD_POOL_SHARDS; s++) {
        pthread_cond_signal(&pool->shards[s].cond);
    }
    pthread_mutex_unlock(&shard->lock);

    return 1;
}

/**
 * @brief Destroy the pool, wait for workers, and free pending tasks.
 *
 * @param pool Pool to destroy.
 */
void ttak_thread_pool_destroy(ttak_thread_pool_t *pool) {
    if (!pool) return;

    pool_force_shutdown(pool);

    for (size_t i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->workers[i]->thread, NULL);
        ttak_mem_free(pool->workers[i]->wrapper);
        ttak_mem_free(pool->workers[i]);
    }

    ttak_mem_free(pool->workers);
    pthread_mutex_destroy(&pool->pool_lock);
    pthread_cond_destroy(&pool->task_cond);

    /* Drain any remaining tasks from each shard and tear down shard resources */
    for (size_t s = 0; s < TTAK_THREAD_POOL_SHARDS; s++) {
        ttak_task_t *t;
        while ((t = pool->shards[s].queue.pop(&pool->shards[s].queue, pool->creation_ts)) != NULL) {
            ttak_task_destroy(t, pool->creation_ts);
        }
        pthread_mutex_destroy(&pool->shards[s].lock);
        pthread_cond_destroy(&pool->shards[s].cond);
    }

    ttak_mem_free(pool);
}
