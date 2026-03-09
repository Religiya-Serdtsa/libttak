/**
 * @file promise.h
 * @brief One-shot promise for fulfilling a ttak_future_t from a worker thread.
 *
 * Create a promise with ttak_promise_create(), obtain its future with
 * ttak_promise_get_future(), and fulfil it from the worker thread via
 * ttak_promise_set_value().
 */

#ifndef TTAK_ASYNC_PROMISE_H
#define TTAK_ASYNC_PROMISE_H

#include <stdint.h>
#include <ttak/async/future.h>

/**
 * @brief Promise handle that pairs with exactly one ttak_future_t.
 */
typedef struct ttak_promise {
    ttak_future_t *future; /**< The future this promise will fulfil. */
} ttak_promise_t;

/**
 * @brief Allocates and initialises a new promise/future pair.
 *
 * @param now Current monotonic timestamp in nanoseconds.
 * @return    Pointer to the new promise, or NULL on allocation failure.
 */
ttak_promise_t *ttak_promise_create(uint64_t now);

/**
 * @brief Fulfils the promise with a result value, unblocking any waiter.
 *
 * @param promise Promise to fulfil.
 * @param val     Result pointer to store in the paired future.
 * @param now     Current monotonic timestamp in nanoseconds.
 */
void ttak_promise_set_value(ttak_promise_t *promise, void *val, uint64_t now);

/**
 * @brief Returns the future associated with this promise.
 *
 * @param promise Source promise.
 * @return        Pointer to the paired future.
 */
ttak_future_t *ttak_promise_get_future(ttak_promise_t *promise);

#endif // TTAK_ASYNC_PROMISE_H
