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
 * @brief Initialize a read-write lock.
 *
 * @param rwlock RWLock to initialize.
 * @return pthread error code.
 */
int ttak_rwlock_init(ttak_rwlock_t *rwlock) {
    return pthread_rwlock_init(rwlock, NULL);
}

/**
 * @brief Acquire a read lock.
 *
 * @param rwlock RWLock to lock for reading.
 * @return pthread error code.
 */
int ttak_rwlock_rdlock(ttak_rwlock_t *rwlock) {
    return pthread_rwlock_rdlock(rwlock);
}

/**
 * @brief Acquire a write lock.
 *
 * @param rwlock RWLock to lock for writing.
 * @return pthread error code.
 */
int ttak_rwlock_wrlock(ttak_rwlock_t *rwlock) {
    return pthread_rwlock_wrlock(rwlock);
}

/**
 * @brief Release a read or write lock.
 *
 * @param rwlock RWLock to unlock.
 * @return pthread error code.
 */
int ttak_rwlock_unlock(ttak_rwlock_t *rwlock) {
    return pthread_rwlock_unlock(rwlock);
}

/**
 * @brief Destroy a read-write lock.
 *
 * @param rwlock RWLock to destroy.
 * @return pthread error code.
 */
int ttak_rwlock_destroy(ttak_rwlock_t *rwlock) {
    return pthread_rwlock_destroy(rwlock);
}

/**
 * @brief Initialize a shard.
 *
 * @param shard Shard to initialize.
 * @param data Data to associate with the shard.
 * @return pthread error code for lock initialization.
 */
int ttak_shard_init(ttak_shard_t *shard, void *data) {
    shard->data = data;
    return ttak_rwlock_init(&shard->lock);
}

/**
 * @brief Destroy a shard.
 *
 * @param shard Shard to destroy.
 * @return pthread error code for lock destruction.
 */
int ttak_shard_destroy(ttak_shard_t *shard) {
    return ttak_rwlock_destroy(&shard->lock);
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
