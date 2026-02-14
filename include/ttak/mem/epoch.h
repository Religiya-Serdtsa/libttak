#ifndef TTAK_MEM_EPOCH_H
#define TTAK_MEM_EPOCH_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

/**
 * Handle tcc's lack of C11 support for _Atomic and _Thread_local.
 */
#if defined(__TINYC__)
    #undef _Atomic
    #define _Atomic
    /* TCC doesn't support _Thread_local / __thread in many environments */
    #ifndef _Thread_local
        #define _Thread_local
    #endif
#endif

#define TTAK_EPOCH_SESSIONS 3

typedef struct ttak_retired_node {
    void *ptr;
    void (*cleanup)(void *);
    struct ttak_retired_node *next;
} ttak_retired_node_t;

typedef struct {
    unsigned int _Atomic global_epoch;
    ttak_retired_node_t * _Atomic retired_queues[TTAK_EPOCH_SESSIONS];
} ttak_epoch_manager_t;

typedef struct {
    unsigned int _Atomic local_epoch;
    bool _Atomic active;
} ttak_thread_state_t;

extern ttak_epoch_manager_t g_epoch_mgr;

/* For tcc compatibility, avoid combining extern and _Thread_local if possible, 
   or ensure the compiler-specific keyword is used. */
#if defined(__TINYC__)
    extern ttak_thread_state_t *t_local_state;
#else
    extern _Thread_local ttak_thread_state_t *t_local_state;
#endif

/**
 * @brief Registers the current thread with the epoch manager.
 */
void ttak_epoch_register_thread(void);

/**
 * @brief Deregisters the current thread.
 */
void ttak_epoch_deregister_thread(void);

/**
 * @brief Enters a critical section protected by EBR.
 */
void ttak_epoch_enter(void);

/**
 * @brief Exits the EBR critical section.
 */
void ttak_epoch_exit(void);

/**
 * @brief Retires a pointer for deferred reclamation.
 * * @param ptr The pointer to retire.
 * @param cleanup Optional cleanup function (e.g., free).
 */
void ttak_epoch_retire(void *ptr, void (*cleanup)(void *));

/**
 * @brief Attempts to reclaim memory from safe epochs.
 */
void ttak_epoch_reclaim(void);

#endif // TTAK_MEM_EPOCH_H
