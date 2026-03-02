#include <ttak/async/promise.h>
#include <ttak/mem/mem.h>
#include <stdlib.h>
#include <string.h>
/**
 * @brief Create a promise/future pair that lives for the process lifetime.
 *
 * Memory is tracked via `__TTAK_UNSAFE_MEM_FOREVER__` and must therefore be
 * explicitly reclaimed by the caller.
 *
 * @param now Current timestamp for memory allocation tracking.
 * @return Pointer to the created promise, or NULL on allocation failure.
 */

ttak_promise_t *ttak_promise_create(uint64_t now) {
    ttak_promise_t *promise = (ttak_promise_t *)ttak_mem_alloc(sizeof(ttak_promise_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!promise) return NULL;
    memset(promise, 0, sizeof(*promise));

    promise->future = (ttak_future_t *)ttak_mem_alloc(sizeof(ttak_future_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!promise->future) {
        ttak_mem_free(promise);
        return NULL;
    }
    memset(promise->future, 0, sizeof(*promise->future));

    pthread_mutex_init(&promise->future->mutex, NULL);
    pthread_cond_init(&promise->future->cond, NULL);

    return promise;
}

/**
 * @brief Fulfill the promise and notify the waiting future.
 *
 * Updates the stored result, marks the future as ready, and wakes any waiters.
 *
 * @param promise Promise to resolve.
 * @param val Pointer to the resolved value.
 * @param now Timestamp for memory access validation.
 */

void ttak_promise_set_value(ttak_promise_t *promise, void *val, uint64_t now) {
    ttak_promise_t *safe_promise = (ttak_promise_t *)ttak_mem_access(promise, now);
    if (!safe_promise) return;

    ttak_future_t *future = (ttak_future_t *)ttak_mem_access(safe_promise->future, now);
    if (!future) return;

    pthread_mutex_lock(&future->mutex);
    future->result = val;
    future->ready = true;
    pthread_cond_broadcast(&future->cond);
    pthread_mutex_unlock(&future->mutex);
}

/**
 * @brief Retrieve the future associated with a promise.
 *
 * @param promise Promise that owns the future.
 * @return Pointer to the future or NULL if the promise is invalid.
 */

ttak_future_t *ttak_promise_get_future(ttak_promise_t *promise) {
    return promise ? promise->future : NULL;
}
