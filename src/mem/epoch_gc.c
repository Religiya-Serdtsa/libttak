#include <ttak/mem/epoch_gc.h>
#include <ttak/mem/mem.h>
#include <ttak/mem/epoch.h>
#include <ttak/timing/timing.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
static inline void epoch_gc_clock_gettime(struct timespec *spec) {
    FILETIME ft;
    uint64_t tim;
    GetSystemTimePreciseAsFileTime(&ft);
    tim = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
    tim -= 116444736000000000ULL; /* Windows epoch diff */
    spec->tv_sec = (time_t)(tim / 1000000000ULL);
    spec->tv_nsec = (long)((tim % 1000000000ULL) * 100);
}
#else
#  define epoch_gc_clock_gettime(spec) clock_gettime(CLOCK_REALTIME, (spec))
#endif

/* Target ~4 kHz wakeups by default but keep within typical embedded timer limits. */
#define TTAK_EPOCH_GC_MIN_ROTATE_NS TT_MICRO_SECOND(100)
#define TTAK_EPOCH_GC_MAX_ROTATE_NS TT_MILLI_SECOND(1)

static void *epoch_gc_rotate_thread(void *arg) {
    ttak_epoch_gc_t *gc = (ttak_epoch_gc_t *)arg;
    if (!gc) return NULL;

    uint64_t sleep_ns = atomic_load(&gc->min_rotate_ns);

    while (!atomic_load(&gc->shutdown_requested)) {
        if (!atomic_load(&gc->manual_rotation)) {
            ttak_epoch_gc_rotate(gc);
            ttak_epoch_reclaim();
            sleep_ns = atomic_load(&gc->min_rotate_ns);
        } else {
            sleep_ns = atomic_load(&gc->max_rotate_ns);
        }

        pthread_mutex_lock(&gc->rotate_lock);
        if (!atomic_load(&gc->shutdown_requested)) {
            struct timespec ts;
            epoch_gc_clock_gettime(&ts);
            ts.tv_sec += (time_t)(sleep_ns / 1000000000ULL);
            ts.tv_nsec += (long)(sleep_ns % 1000000000ULL);
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&gc->rotate_cond, &gc->rotate_lock, &ts);
        }
        pthread_mutex_unlock(&gc->rotate_lock);
    }

    return NULL;
}

/**
 * @brief Initializes the Epoch GC structure.
 * 
 * Sets up the underlying memory tree with manual cleanup mode enabled,
 * allowing the user to control the garbage collection cycles via ttak_epoch_gc_rotate.
 * 
 * @param gc Pointer to the GC context.
 */
void ttak_epoch_gc_init(ttak_epoch_gc_t *gc) {
    ttak_mem_tree_init(&gc->tree);
    // Enable manual cleanup to prevent automatic background threads from interfering
    // with the user-controlled periodic cycle.
    ttak_mem_tree_set_manual_cleanup(&gc->tree, true);
    atomic_store(&gc->current_epoch, 0);
    atomic_store(&gc->last_cleanup_ts, ttak_get_tick_count());

    pthread_mutex_init(&gc->rotate_lock, NULL);
    pthread_cond_init(&gc->rotate_cond, NULL);
    atomic_store(&gc->shutdown_requested, false);
    atomic_store(&gc->manual_rotation, false);
    atomic_store(&gc->min_rotate_ns, TTAK_EPOCH_GC_MIN_ROTATE_NS);
    atomic_store(&gc->max_rotate_ns, TTAK_EPOCH_GC_MAX_ROTATE_NS);
    gc->rotate_thread_started = false;

    if (pthread_create(&gc->rotate_thread, NULL, epoch_gc_rotate_thread, gc) == 0) {
        gc->rotate_thread_started = true;
    } else {
        fprintf(stderr, "[TTAK][epoch_gc] Failed to launch rotate thread, falling back to manual mode.\n");
        atomic_store(&gc->manual_rotation, true);
    }
}

/**
 * @brief Destroys the GC context.
 * 
 * Frees all remaining memory blocks tracked by the tree.
 * 
 * @param gc Pointer to the GC context.
 */
void ttak_epoch_gc_destroy(ttak_epoch_gc_t *gc) {
    atomic_store(&gc->shutdown_requested, true);
    pthread_mutex_lock(&gc->rotate_lock);
    pthread_cond_signal(&gc->rotate_cond);
    pthread_mutex_unlock(&gc->rotate_lock);

    if (gc->rotate_thread_started) {
        pthread_join(gc->rotate_thread, NULL);
    }

    pthread_cond_destroy(&gc->rotate_cond);
    pthread_mutex_destroy(&gc->rotate_lock);

    ttak_mem_tree_destroy(&gc->tree);
}

/**
 * @brief Registers a memory block with the current epoch.
 * 
 * @param gc Pointer to the GC context.
 * @param ptr Pointer to the memory block.
 * @param size Size of the block.
 */
void ttak_epoch_gc_register(ttak_epoch_gc_t *gc, void *ptr, size_t size) {
    // Add to the underlying mem_tree.
    // The 'expires_tick' (4th arg) is 0 because we manage lifetime via epochs,
    // not strictly by clock time, though mem_tree supports both.
    // We treat these as "roots" for the current epoch.
    ttak_mem_tree_add(&gc->tree, ptr, size, 0, true);
}

/**
 * @brief Rotates the epoch and performs cleanup.
 * 
 * Increments the epoch counter and triggers the memory tree's cleanup logic.
 * This function iterates through the tree, identifying blocks that are no longer
 * referenced or valid, and frees them. It is designed to be called periodically.
 * 
 * @param gc Pointer to the GC context.
 */
void ttak_epoch_gc_rotate(ttak_epoch_gc_t *gc) {
    if (!gc) return;

    atomic_fetch_add(&gc->current_epoch, 1);

    uint64_t now = ttak_get_tick_count();
    // Perform cleanup using the current timestamp.
    // mem_tree_perform_cleanup iterates the tree and removes expired/unreferenced nodes.
    // By controlling when this is called, we avoid "stop-the-world" pauses at arbitrary times.
    ttak_mem_tree_perform_cleanup(&gc->tree, now);

    atomic_store(&gc->last_cleanup_ts, now);
}

void ttak_epoch_gc_manual_rotate(ttak_epoch_gc_t *gc, _Bool manual_mode) {
    if (!gc) return;
    atomic_store(&gc->manual_rotation, manual_mode);
    pthread_mutex_lock(&gc->rotate_lock);
    pthread_cond_signal(&gc->rotate_cond);
    pthread_mutex_unlock(&gc->rotate_lock);
}
