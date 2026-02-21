#include <ttak/mem/epoch.h>
#include <ttak/timing/timing.h>
#include <ttak/mem/mem.h>
#include <stdatomic.h>
#include <string.h>

/* Suppress MSVC C4311/C4312: MSVC's <stdatomic.h> internally casts pointer
 * atomics through 'long' on Windows even though the actual Interlocked ops
 * are 64-bit correct.  The warnings are spurious noise from the header's own
 * macro expansion, not from our code. */
#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable: 4311 4312)
#endif

/* Global instance */
ttak_epoch_manager_t g_epoch_mgr = {0};

/* Thread-local state */
_Thread_local ttak_thread_state_t *t_local_state = NULL;

/* 
 * A simple linked list to track all registered thread states 
 * for the reclaimer to iterate over.
 * In a production system, this should be a lock-free list or array.
 * Here we use a simple lock for registration since it's rare.
 */
typedef struct ttak_thread_node {
    ttak_thread_state_t *state;
    struct ttak_thread_node *next;
} ttak_thread_node_t;

static ttak_thread_node_t * _Atomic g_thread_list = NULL;

void ttak_epoch_register_thread(void) {
    if (t_local_state) return;

    uint64_t now = ttak_get_tick_count();
    t_local_state = ttak_mem_alloc(sizeof(ttak_thread_state_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    atomic_init(&t_local_state->local_epoch, 0);
    atomic_init(&t_local_state->active, false);

    /* Add to global list */
    ttak_thread_node_t *node = ttak_mem_alloc(sizeof(ttak_thread_node_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    node->state = t_local_state;
    
    ttak_thread_node_t *old_head;
    do {
        old_head = atomic_load(&g_thread_list);
        node->next = old_head;
    } while (!atomic_compare_exchange_weak(&g_thread_list, &old_head, node));
}

void ttak_epoch_deregister_thread(void) {
    if (!t_local_state) return;
    
    atomic_store(&t_local_state->active, false);
    /* 
     * Note: We don't remove from the list here to avoid complex concurrency issues
     * in this simplified implementation. The reclaimer will just see an inactive thread.
     * Real-world implementations would mark the node as free or use a generation ID.
     */
    // free(t_local_state); // Leaking the state struct intentionally to keep the list valid for now.
    t_local_state = NULL;
}

void ttak_epoch_enter(void) {
    if (!t_local_state) ttak_epoch_register_thread();
    
    atomic_store_explicit(&t_local_state->active, true, memory_order_release);
    atomic_thread_fence(memory_order_seq_cst);
    
    uint32_t current = atomic_load_explicit(&g_epoch_mgr.global_epoch, memory_order_acquire);
    atomic_store_explicit(&t_local_state->local_epoch, current, memory_order_release);
}

void ttak_epoch_exit(void) {
    if (!t_local_state) return;
    atomic_store_explicit(&t_local_state->active, false, memory_order_release);
}

void ttak_epoch_retire(void *ptr, void (*cleanup)(void *)) {
    if (!ptr) return;

    ttak_retired_node_t *node = ttak_mem_alloc(sizeof(ttak_retired_node_t), __TTAK_UNSAFE_MEM_FOREVER__, ttak_get_tick_count());
    node->ptr = ptr;
    node->cleanup = cleanup;

    uint32_t idx = atomic_load(&g_epoch_mgr.global_epoch) % TTAK_EPOCH_SESSIONS;
    
    /* Lock-free push to retirement queue */
    ttak_retired_node_t *old_head;
    do {
        old_head = atomic_load(&g_epoch_mgr.retired_queues[idx]);
        node->next = old_head;
    } while (!atomic_compare_exchange_weak(&g_epoch_mgr.retired_queues[idx], &old_head, node));
}

void ttak_epoch_reclaim(void) {
    uint32_t current = atomic_load(&g_epoch_mgr.global_epoch);

    /* Check all threads */
    ttak_thread_node_t *curr_node = atomic_load(&g_thread_list);
    while (curr_node) {
        ttak_thread_state_t *state = curr_node->state;
        if (atomic_load(&state->active)) {
            if (atomic_load(&state->local_epoch) != current) {
                return; /* Someone is lagging behind or in a previous epoch */
            }
        }
        curr_node = curr_node->next;
    }

    /* 
     * If we are here, all active threads are in the current epoch.
     * It is safe to reclaim the (current - 2) epoch.
     * In a 3-epoch system (0, 1, 2), if current is 0, we reclaim (0+1)%3 = 1?
     * No, logic is: 
     * Current: E. 
     * Threads can be in E or E-1 (if they entered just before global switch).
     * If all are in E, then E-1 is also safe? 
     * Standard EBR:
     * Global epoch E. Threads verify they are in E.
     * Objects retired in E need to wait until E becomes E-2?
     * Actually:
     * Retire(p) happens in E.
     * We can free p when all threads have moved past E.
     * If global is E, and all threads are E, we can bump global to E+1.
     * Then anything from E-1 (previous) is definitely safe?
     * Let's follow the prompt logic:
     * reclaim_idx = (current + 1) % SESSIONS.
     * This seems to reclaim the "next" bucket, which was the "oldest" bucket?
     * E=0. reclaim_idx=1.
     * E=1. reclaim_idx=2.
     * E=2. reclaim_idx=0.
     * 
     * If we advance global epoch, we effectively "close" the current bucket for new retires,
     * and open the next one.
     */
     
    /* Advance global epoch */
    atomic_fetch_add(&g_epoch_mgr.global_epoch, 1);
    
    /* 
     * Reclaim the "next" bucket in the ring. 
     * Since we just incremented global_epoch, (current + 1) was the *previous* global_epoch's (current+2).
     * Wait, prompt logic:
     * uint32_t reclaim_idx = (current + 1) % TTAK_EPOCH_SESSIONS; 
     * ...
     * atomic_fetch_add(&g_epoch_mgr.global_epoch, 1);
     *
     * Let's trace:
     * Global = 0. We check threads. All are 0.
     * reclaim_idx = 1.
     * Global becomes 1.
     * We free queue[1].
     * New retires go to queue[1] (since 1%3 == 1).
     * 
     * Is queue[1] safe?
     * It contains items retired when Global was 1 (in the previous cycle).
     * We are currently transitioning 0 -> 1.
     * The items in queue[1] were retired when global was 1.
     * We are now leaving 0 and entering 1.
     * So 1 is actually the *current* epoch again?
     * 
     * Let's look at standard EBR 3-epoch.
     * Epochs: 0, 1, 2.
     * Global G. Retire into G.
     * If all threads >= G (or in G), increment G.
     * Then reclaim (G-2).
     * 
     * Prompt logic:
     * reclaim_idx = (current + 1) % 3.
     * current = 0. reclaim = 1.
     * global becomes 1.
     * We free queue[1].
     * 
     * Queue[1] was filled when Global was 1.
     * Then Global became 2. Then Global became 0.
     * Now Global becomes 1.
     * So items in Queue[1] have survived through Global=2 and Global=0.
     * Yes, that's 2 epochs old. Safe.
     */

    uint32_t reclaim_idx = (current + 1) % TTAK_EPOCH_SESSIONS;
    ttak_retired_node_t *to_free = (ttak_retired_node_t *)atomic_exchange(&g_epoch_mgr.retired_queues[reclaim_idx], NULL);

    while (to_free) {
        ttak_retired_node_t *next = to_free->next;
        if (to_free->cleanup) to_free->cleanup(to_free->ptr);
        ttak_mem_free(to_free);
        to_free = next;
    }
}

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
