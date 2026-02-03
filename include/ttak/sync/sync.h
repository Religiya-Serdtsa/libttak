#ifndef TTAK_SYNC_H
#define TTAK_SYNC_H

#include <pthread.h>
#include <stdint.h>

typedef pthread_mutex_t ttak_mutex_t;
typedef pthread_mutex_t tt_mutex_t;

typedef pthread_cond_t ttak_cond_t;
typedef pthread_cond_t tt_cond_t;

typedef pthread_rwlock_t ttak_rwlock_t;
typedef pthread_rwlock_t tt_rwlock_t;

/**
 * @brief Generic shard structure.
 */
typedef struct ttak_shard {
    void            *data;
    ttak_rwlock_t   lock;
} ttak_shard_t;

typedef ttak_shard_t tt_shard_t;

/**
 * @brief Shared resource structure.
 */
typedef struct tt_type_shared {
    void            *data;
    int             size;
    pthread_mutex_t mutex;
    uint64_t        ts;    /** Inherited timestamp (tick count) */
} tt_type_shared_t;

typedef tt_type_shared_t ttak_type_shared_t;

// Mutex
int ttak_mutex_init(ttak_mutex_t *mutex);
int ttak_mutex_lock(ttak_mutex_t *mutex);
int ttak_mutex_unlock(ttak_mutex_t *mutex);
int ttak_mutex_destroy(ttak_mutex_t *mutex);

// RWLock
int ttak_rwlock_init(ttak_rwlock_t *rwlock);
int ttak_rwlock_rdlock(ttak_rwlock_t *rwlock);
int ttak_rwlock_wrlock(ttak_rwlock_t *rwlock);
int ttak_rwlock_unlock(ttak_rwlock_t *rwlock);
int ttak_rwlock_destroy(ttak_rwlock_t *rwlock);

// Shard
int ttak_shard_init(ttak_shard_t *shard, void *data);
int ttak_shard_destroy(ttak_shard_t *shard);

// Condition Variable
int ttak_cond_init(ttak_cond_t *cond);
int ttak_cond_wait(ttak_cond_t *cond, ttak_mutex_t *mutex);
int ttak_cond_signal(ttak_cond_t *cond);
int ttak_cond_broadcast(ttak_cond_t *cond);
int ttak_cond_destroy(ttak_cond_t *cond);

#endif // TTAK_SYNC_H