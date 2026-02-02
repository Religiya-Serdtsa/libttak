#include <ttak/sync/sync.h>

/**
 * @brief Initialize a mutex wrapper.
 *
 * @param mutex Mutex to initialize.
 * @return pthread error code.
 */
int ttak_mutex_init(ttak_mutex_t *mutex) {
    return pthread_mutex_init(mutex, NULL);
}

/**
 * @brief Acquire the mutex.
 *
 * @param mutex Mutex to lock.
 * @return pthread error code.
 */
int ttak_mutex_lock(ttak_mutex_t *mutex) {
    return pthread_mutex_lock(mutex);
}

/**
 * @brief Release the mutex.
 *
 * @param mutex Mutex to unlock.
 * @return pthread error code.
 */
int ttak_mutex_unlock(ttak_mutex_t *mutex) {
    return pthread_mutex_unlock(mutex);
}

/**
 * @brief Destroy the mutex.
 *
 * @param mutex Mutex to destroy.
 * @return pthread error code.
 */
int ttak_mutex_destroy(ttak_mutex_t *mutex) {
    return pthread_mutex_destroy(mutex);
}

/**
 * @brief Initialize a condition variable.
 *
 * @param cond Condition variable to initialize.
 * @return pthread error code.
 */
int ttak_cond_init(ttak_cond_t *cond) {
    return pthread_cond_init(cond, NULL);
}

/**
 * @brief Wait on a condition variable.
 *
 * @param cond  Condition variable.
 * @param mutex Associated mutex (locked on entry).
 * @return pthread error code.
 */
int ttak_cond_wait(ttak_cond_t *cond, ttak_mutex_t *mutex) {
    return pthread_cond_wait(cond, mutex);
}

/**
 * @brief Wake one waiter on the condition variable.
 *
 * @param cond Condition variable.
 * @return pthread error code.
 */
int ttak_cond_signal(ttak_cond_t *cond) {
    return pthread_cond_signal(cond);
}

/**
 * @brief Wake all waiters on the condition variable.
 *
 * @param cond Condition variable.
 * @return pthread error code.
 */
int ttak_cond_broadcast(ttak_cond_t *cond) {
    return pthread_cond_broadcast(cond);
}

/**
 * @brief Destroy the condition variable.
 *
 * @param cond Condition variable to destroy.
 * @return pthread error code.
 */
int ttak_cond_destroy(ttak_cond_t *cond) {
    return pthread_cond_destroy(cond);
}
