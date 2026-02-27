#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ttak/mem/mem.h>
#include <ttak/mem/epoch.h>
#include <ttak/timing/timing.h>
#include <stdatomic.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

/**
 * @file epoch.c
 * @brief Epoch-Based Reclamation (EBR) via 16x16 OLS mapping.
 * Optimized for LTO and background GC thread stability.
 */

#define OLS_ORDER 16

typedef struct {
    ttak_retired_node_t * _Atomic buckets[OLS_ORDER][OLS_ORDER];
    size_t _Atomic occupancy[OLS_ORDER];
} ttak_ols_plane_t;

/* Global state with explicit default visibility for LTO */
__attribute__((visibility("default"))) ttak_epoch_manager_t g_epoch_mgr = {0};
__attribute__((visibility("default"))) ttak_ols_plane_t g_ols_static_plane = { .occupancy = {0} };
__attribute__((visibility("default"))) bool _Atomic g_epoch_init_ready = false;

static pthread_mutex_t g_reclaim_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct ttak_thread_node {
    ttak_thread_state_t *state;
    uint32_t logical_tid;
    struct ttak_thread_node *next;
} ttak_thread_node_t;

#if !defined(__TINYC__)
__attribute__((visibility("default"))) _Thread_local ttak_thread_state_t *t_local_state = NULL;
#else
__attribute__((visibility("default"))) ttak_thread_state_t *t_local_state = NULL;
#endif

__attribute__((visibility("default"))) ttak_thread_node_t * _Atomic g_thread_list = NULL;
__attribute__((visibility("default"))) uint32_t _Atomic g_tid_counter = 0;

/**
 * @brief Initialize OLS grid buckets.
 */
__attribute__((constructor(101)))
void ttak_epoch_subsystem_init(void) {
    if (atomic_load(&g_epoch_init_ready)) return;

    for (int i = 0; i < OLS_ORDER; i++) {
        for (int j = 0; j < OLS_ORDER; j++) {
            atomic_init(&g_ols_static_plane.buckets[i][j], NULL);
        }
        atomic_init(&g_ols_static_plane.occupancy[i], 0);
    }
    atomic_store(&g_epoch_init_ready, true);
}

static inline void get_ols_coords(uint32_t tid, uint32_t gen, int *x, int *y) {
    *x = (int)(tid % OLS_ORDER);
    *y = (int)((tid / OLS_ORDER + gen) % OLS_ORDER);
}

void ttak_epoch_register_thread(void) {
    if (t_local_state) return;
    if (!atomic_load(&g_epoch_init_ready)) ttak_epoch_subsystem_init();

    t_local_state = (ttak_thread_state_t *)ttak_dangerous_calloc(1, sizeof(ttak_thread_state_t));
    atomic_init(&t_local_state->local_epoch, 0);
    atomic_init(&t_local_state->active, false);

    ttak_thread_node_t *node = (ttak_thread_node_t *)ttak_dangerous_calloc(1, sizeof(ttak_thread_node_t));
    node->state = t_local_state;
    node->logical_tid = atomic_fetch_add_explicit(&g_tid_counter, 1, memory_order_relaxed);
    t_local_state->logical_tid = node->logical_tid;
    
    ttak_thread_node_t *old_head;
    do {
        old_head = atomic_load_explicit(&g_thread_list, memory_order_acquire);
        node->next = old_head;
    } while (!atomic_compare_exchange_weak_explicit(&g_thread_list, &old_head, node, 
                                                    memory_order_release, memory_order_acquire));
}

void ttak_epoch_deregister_thread(void) {
    if (!t_local_state) return;
    atomic_store_explicit(&t_local_state->active, false, memory_order_seq_cst);
    /* We don't free t_local_state here to avoid race in reclaim, but we mark it inactive. */
    t_local_state = NULL;
}

void ttak_epoch_enter(void) {
    if (!t_local_state) ttak_epoch_register_thread();
    
    uint32_t current = atomic_load_explicit(&g_epoch_mgr.global_epoch, memory_order_acquire);
    atomic_store_explicit(&t_local_state->local_epoch, current, memory_order_release);
    atomic_store_explicit(&t_local_state->active, true, memory_order_release);
    atomic_thread_fence(memory_order_seq_cst);
}

void ttak_epoch_exit(void) {
    if (!t_local_state) return;
    atomic_store_explicit(&t_local_state->active, false, memory_order_release);
}

void ttak_epoch_retire(void *ptr, void (*cleanup)(void *)) {
    if (!ptr) return;
    if (!t_local_state) ttak_epoch_register_thread();

    ttak_retired_node_t *node = (ttak_retired_node_t *)ttak_dangerous_calloc(1, sizeof(ttak_retired_node_t));
    node->ptr = ptr;
    node->cleanup = cleanup;

    uint32_t gen = atomic_load_explicit(&g_epoch_mgr.global_epoch, memory_order_acquire);
    uint32_t tid = t_local_state->logical_tid;

    int x, y;
    get_ols_coords(tid, gen, &x, &y);

    ttak_retired_node_t *old_head;
    do {
        old_head = atomic_load_explicit(&g_ols_static_plane.buckets[x][y], memory_order_acquire);
        node->next = old_head;
    } while (!atomic_compare_exchange_weak_explicit(&g_ols_static_plane.buckets[x][y], &old_head, node,
                                                    memory_order_release, memory_order_acquire));
}

void ttak_epoch_reclaim(void) {
    /* Critical: Prevent background GC from touching uninitialized OLS plane */
    if (!atomic_load(&g_epoch_init_ready)) return;
    if (pthread_mutex_trylock(&g_reclaim_lock) != 0) return;

    uint32_t current = atomic_load_explicit(&g_epoch_mgr.global_epoch, memory_order_acquire);
    bool safe = true;

    atomic_thread_fence(memory_order_seq_cst);

    ttak_thread_node_t *node = atomic_load_explicit(&g_thread_list, memory_order_acquire);
    while (node) {
        if (atomic_load_explicit(&node->state->active, memory_order_acquire)) {
            if (atomic_load_explicit(&node->state->local_epoch, memory_order_acquire) < current) {
                safe = false;
                break;
            }
        }
        node = node->next;
    }

    if (safe) {
        atomic_fetch_add_explicit(&g_epoch_mgr.global_epoch, 1, memory_order_acq_rel);
        
        for (int i = 0; i < OLS_ORDER; i++) {
            for (int j = 0; j < OLS_ORDER; j++) {
                ttak_retired_node_t *to_free = atomic_exchange_explicit(&g_ols_static_plane.buckets[i][j], NULL, memory_order_acq_rel);
                while (to_free) {
                    ttak_retired_node_t *next = to_free->next;
                    if (to_free->cleanup) to_free->cleanup(to_free->ptr);
                    ttak_dangerous_free(to_free);
                    to_free = next;
                }
            }
        }
    }
    pthread_mutex_unlock(&g_reclaim_lock);
}
