/**
 * @file future.h
 * @brief Simple blocking future for async task result retrieval.
 *
 * A @c ttak_future_t is produced by ttak_promise_get_future() and consumed
 * by calling ttak_future_get(), which blocks the caller until the associated
 * promise is fulfilled.
 */

#ifndef TTAK_ASYNC_FUTURE_H
#define TTAK_ASYNC_FUTURE_H

#include <pthread.h>
#include <stdbool.h>

/**
 * @brief Blocking future that carries a single void* result.
 */
typedef struct ttak_future {
    void            *result; /**< Result pointer set by the fulfilling promise. */
    _Bool           ready;   /**< Non-zero once the result is available. */
    pthread_mutex_t mutex;   /**< Guards @c ready and @c result. */
    pthread_cond_t  cond;    /**< Signalled when @c ready becomes true. */
} ttak_future_t;

/**
 * @brief Blocks until the future is fulfilled and returns its result.
 *
 * @param future Pointer to an initialised future.
 * @return       The result pointer set by the corresponding promise.
 */
void *ttak_future_get(ttak_future_t *future);

#endif // TTAK_ASYNC_FUTURE_H
