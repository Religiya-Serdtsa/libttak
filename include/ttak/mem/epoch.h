#ifndef TTAK_MEM_EPOCH_H
#define TTAK_MEM_EPOCH_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define TTAK_EPOCH_SESSIONS 3

typedef struct ttak_retired_node {
    void *ptr;
    void (*cleanup)(void *);
    struct ttak_retired_node *next;
} ttak_retired_node_t;

typedef struct {
    atomic_uint global_epoch;
    _Atomic(ttak_retired_node_t *) retired_queues[TTAK_EPOCH_SESSIONS];
} ttak_epoch_manager_t;

typedef struct {
    atomic_uint local_epoch;
    atomic_bool active;
} ttak_thread_state_t;

extern ttak_epoch_manager_t g_epoch_mgr;
extern _Thread_local ttak_thread_state_t *t_local_state;

/**
 * @brief Registers the current thread with the epoch manager.
 * Must be called once per thread before using EBR features.
 */
void ttak_epoch_register_thread(void);

/**
 * @brief Deregisters the current thread.
 * Call when the thread is about to exit.
 */
void ttak_epoch_deregister_thread(void);

/**
 * @brief Enters a critical section protected by EBR.
 * Ensures that objects accessed inside this section are not reclaimed.
 */
void ttak_epoch_enter(void);

/**
 * @brief Exits the EBR critical section.
 */
void ttak_epoch_exit(void);

/**
 * @brief Retires a pointer for deferred reclamation.
 * 
 * @param ptr The pointer to retire.
 * @param cleanup Optional cleanup function (e.g., free).
 */
void ttak_epoch_retire(void *ptr, void (*cleanup)(void *));

/**
 * @brief Attempts to reclaim memory from safe epochs.
 * Should be called periodically or when memory pressure is high.
 */
void ttak_epoch_reclaim(void);

#endif // TTAK_MEM_EPOCH_H
