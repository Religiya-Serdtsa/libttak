#ifndef TTAK_MEM_EPOCH_H
#define TTAK_MEM_EPOCH_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <ttak/types/ttak_compiler.h>

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
    uint32_t logical_tid;
} ttak_thread_state_t;

extern ttak_epoch_manager_t g_epoch_mgr;

/**
 * @brief Registers the current thread with the epoch manager.
 */
void ttak_epoch_register_thread(void);

/**
 * @brief Deregisters the current thread.
 */
void ttak_epoch_deregister_thread(void);

#if defined(__TINYC__)
extern ttak_thread_state_t *ttak_get_t_local_state(void);
extern void ttak_set_t_local_state(ttak_thread_state_t *val);
#define t_local_state ttak_get_t_local_state()

#define ttak_epoch_enter() do { \
    ttak_thread_state_t *__st = ttak_get_t_local_state(); \
    if (TTAK_UNLIKELY(!__st)) { ttak_epoch_register_thread(); __st = ttak_get_t_local_state(); } \
    if (TTAK_LIKELY(__st)) { \
        unsigned int __current = atomic_load_explicit(&g_epoch_mgr.global_epoch, memory_order_acquire); \
        atomic_store_explicit(&__st->local_epoch, __current, memory_order_relaxed); \
        atomic_store_explicit(&__st->active, true, memory_order_seq_cst); \
    } \
} while(0)

#define ttak_epoch_exit() do { \
    ttak_thread_state_t *__st = ttak_get_t_local_state(); \
    if (TTAK_LIKELY(__st)) { \
        atomic_store_explicit(&__st->active, false, memory_order_release); \
    } \
} while(0)
#else
extern _Thread_local ttak_thread_state_t *t_local_state;

/**
 * @brief Enters a critical section protected by EBR.
 */
TTAK_FORCE_INLINE void ttak_epoch_enter(void) {
    if (TTAK_UNLIKELY(!t_local_state)) {
        ttak_epoch_register_thread();
    }
    if (TTAK_UNLIKELY(!t_local_state)) return;

    unsigned int current = atomic_load_explicit(&g_epoch_mgr.global_epoch, memory_order_acquire);
    atomic_store_explicit(&t_local_state->local_epoch, current, memory_order_relaxed);
    atomic_store_explicit(&t_local_state->active, true, memory_order_seq_cst);
}

/**
 * @brief Exits the EBR critical section.
 */
TTAK_FORCE_INLINE void ttak_epoch_exit(void) {
    if (TTAK_UNLIKELY(!t_local_state)) return;
    atomic_store_explicit(&t_local_state->active, false, memory_order_release);
}
#endif

/**
 * @brief Retires a pointer for deferred reclamation.
 * @param ptr The pointer to retire.
 * @param cleanup Optional cleanup function (e.g., free).
 */
void ttak_epoch_retire(void *ptr, void (*cleanup)(void *));

/**
 * @brief Attempts to reclaim memory from safe epochs.
 */
void ttak_epoch_reclaim(void);

#endif // TTAK_MEM_EPOCH_H
