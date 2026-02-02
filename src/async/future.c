#include <ttak/async/future.h>
#include <stddef.h>

/**
 * @brief Retrieve the computed value stored in a future.
 *
 * Blocks until the future becomes ready.
 *
 * @param future Future to read from.
 * @return Result pointer provided by the producer, or NULL if the input is invalid.
 */
void *ttak_future_get(ttak_future_t *future) {
    if (!future) return NULL;
    pthread_mutex_lock(&future->mutex);
    while (!future->ready) {
        pthread_cond_wait(&future->cond, &future->mutex); // wait until the future structure is ready.
    }
    void *res = future->result; // the result is already casted; Don't cast future->result again. That is useless.
    pthread_mutex_unlock(&future->mutex); // retrieved a the result. Unlocking.
    return res;
}
