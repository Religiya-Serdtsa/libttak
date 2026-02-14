#include <ttak/mem/mem.h>
#include <ttak/atomic/atomic.h>
#include <ttak/ht/map.h>
#include <ttak/timing/timing.h>
#include <ttak/mem_tree/mem_tree.h> // Include for mem tree integration
#include "../../internal/app_types.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include <errno.h>

// Windows header stuff
#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #define fsync(fd) _commit(fd)
    #define unlink _unlink
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

#ifndef MAP_HUGETLB
    #define MAP_HUGETLB 0x0
#endif

// Windows MSVC/MinGW stuff
#ifdef _WIN32
static int posix_memalign(void **memptr, size_t alignment, size_t size) {
    *memptr = _aligned_malloc(size, alignment);
    return (*memptr) ? 0 : 1;
}
#ifndef MAP_FAILED
    #define MAP_FAILED ((void *)-1)
#endif
#define posix_memfree(ptr) _aligned_free(ptr)
#else
#define posix_memfree(ptr) free(ptr)
#endif

#include <stdalign.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>

#define TTAK_CANARY_START_MAGIC 0xDEADBEEFDEADBEEFULL
#define TTAK_CANARY_END_MAGIC   0xBEEFDEADBEEFDEADULL

static volatile uint64_t global_mem_usage = 0;
static pthread_mutex_t global_map_lock = PTHREAD_MUTEX_INITIALIZER;
static ttak_mem_tree_t global_mem_tree; // Global instance of the mem tree
static int global_trace_enabled = 0;

/**
 * @brief Internal validation macro.
 */
#define V_HEADER(ptr) do { \
    if (!ptr) break; \
    ttak_mem_header_t *_h = (ttak_mem_header_t *)(ptr) - 1; \
    if (_h->magic != TTAK_MAGIC_NUMBER || _h->checksum != ttak_calc_header_checksum(_h)) { \
        fprintf(stderr, "[FATAL] TTAK Memory Corruption detected at %p (Header corrupted)\n", (void*)ptr); \
        abort(); \
    } \
    if (_h->strict_check) { \
        if (_h->canary_start != TTAK_CANARY_START_MAGIC) { \
            fprintf(stderr, "[FATAL] TTAK Memory Corruption detected at %p (Start canary corrupted in header)\n", (void*)ptr); \
            abort(); \
        } \
        uint64_t *canary_end_ptr = (uint64_t *)((char *)ptr + _h->size); \
        if (*canary_end_ptr != TTAK_CANARY_END_MAGIC) { \
            fprintf(stderr, "[FATAL] TTAK Memory Corruption detected at %p (End canary corrupted)\n", (void*)ptr); \
            abort(); \
        } \
    } \
} while(0)

#define GET_HEADER(ptr) ((ttak_mem_header_t *)(ptr) - 1)
#define GET_USER_PTR(header) ((void *)((ttak_mem_header_t *)(header) + 1))

static volatile tt_map_t *global_ptr_map = NULL;
static pthread_mutex_t global_init_lock = PTHREAD_MUTEX_INITIALIZER;
TTAK_THREAD_LOCAL int in_mem_op = 0;
TTAK_THREAD_LOCAL int in_mem_init = 0;
static volatile int global_init_done = 0;

/**
 * @brief Returns whether memory tracing is currently enabled.
 */
int ttak_mem_is_trace_enabled(void) {
    return global_trace_enabled;
}

/**
 * @brief Toggles memory tracing globally and for all existing allocations.
 */
void ttak_mem_set_trace(int enable) {
    global_trace_enabled = enable;
    if (!global_init_done) return;

    pthread_mutex_lock(&global_map_lock);
    tt_map_t *map_handle = (tt_map_t *)global_ptr_map;
    if (map_handle) {
        for (size_t i = 0; i < map_handle->cap; i++) {
            if (map_handle->tbl[i].ctrl == OCCUPIED) {
                ttak_mem_header_t *h = (ttak_mem_header_t *)map_handle->tbl[i].value;
                pthread_mutex_lock(&h->lock);
                if (enable && !h->tracking_log) {
                    h->tracking_log = malloc(1024);
                    if (h->tracking_log) {
                        snprintf(h->tracking_log, 1024, "{\"event\":\"trace_enabled\",\"ts\":%lu}", ttak_get_tick_count());
                    }
                } else if (!enable && h->tracking_log) {
                    free(h->tracking_log);
                    h->tracking_log = NULL;
                }
                pthread_mutex_unlock(&h->lock);
            }
        }
    }
    pthread_mutex_unlock(&global_map_lock);
}

/**
 * @brief Lazily initialize the global pointer map and mem tree.
 *
 * @param now Timestamp propagated to the map allocator.
 */
static void ensure_global_map(uint64_t now) {
    if (global_init_done || in_mem_init) return;

    pthread_mutex_lock(&global_init_lock);
    if (!global_ptr_map && !global_init_done) {
        in_mem_init = true;
        global_ptr_map = ttak_create_map(8192, now);
        ttak_mem_tree_init(&global_mem_tree); // Initialize the global mem tree
        global_init_done = true;
        in_mem_init = false;
    }
    pthread_mutex_unlock(&global_init_lock);
}

/**
 * @brief Allocate tracked memory with metadata headers and optional strict checks.
 *
 * @param size           Number of bytes requested.
 * @param lifetime_ticks Lifetime hint for automatic reclamation.
 * @param now            Current timestamp.
 * @param is_const       Marks the buffer as immutable.
 * @param is_volatile    Indicates volatile access patterns.
 * @param allow_direct   If false, ttak_mem_access will refuse direct pointers.
 * @param is_root        Marks the allocation as externally referenced for the mem tree.
 * @param flags          Allocation behavior flags.
 * @return Pointer to zeroed user memory or NULL on failure.
 */
void TTAK_HOT_PATH *ttak_mem_alloc_safe(size_t size, uint64_t lifetime_ticks, uint64_t now, _Bool is_const, _Bool is_volatile, _Bool allow_direct, _Bool is_root, ttak_mem_flags_t flags) {
    size_t header_size = sizeof(ttak_mem_header_t);
    bool strict_check_enabled = (flags & TTAK_MEM_STRICT_CHECK);
    size_t canary_padding = strict_check_enabled ? sizeof(uint64_t) : 0;
    size_t total_alloc_size = header_size + canary_padding + size;
    ttak_mem_header_t *header = NULL;
    bool is_huge = false;

    if (flags & TTAK_MEM_HUGE_PAGES) {
#ifdef _WIN32
        header = VirtualAlloc(NULL, total_alloc_size, MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES, PAGE_READWRITE);
#else
        header = mmap(NULL, total_alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
#endif
        if (header != MAP_FAILED) {
            is_huge = true;
        } else {
            header = NULL;
        }
    }

    if (!header) {
        // Default to 64-byte alignment anyway to satisfy SIMD and prevent false sharing
        if (posix_memalign((void **)&header, 64, total_alloc_size) != 0) {
            header = NULL;
        }
    }

    if (!header && errno == ENOMEM) {
        static TTAK_THREAD_LOCAL int retrying = 0;
        if (!retrying) {
            retrying = 1;
            tt_autoclean_dirty_pointers(now);
            void *res = ttak_mem_alloc_safe(size, lifetime_ticks, now, is_const, is_volatile, allow_direct, is_root, flags);
            retrying = 0;
            return res;
        }
    }

    if (!header) return NULL;

    header->magic = TTAK_MAGIC_NUMBER;
    header->created_tick = now;
    header->expires_tick = (lifetime_ticks == __TTAK_UNSAFE_MEM_FOREVER__) ? (uint64_t)-1 : now + lifetime_ticks;
    header->access_count = 0;
    header->pin_count = 0;
    header->size = size;
    header->freed = false;
    header->is_const = is_const;
    header->is_volatile = is_volatile;
    header->allow_direct_access = allow_direct;
    header->is_huge = is_huge;
    header->should_join = false; // Default to false, can be set later if needed
    header->strict_check = strict_check_enabled;
    header->is_root = is_root;
    header->canary_start = strict_check_enabled ? TTAK_CANARY_START_MAGIC : 0;
    header->canary_end = strict_check_enabled ? TTAK_CANARY_END_MAGIC : 0;
    pthread_mutex_init(&header->lock, NULL);
    header->checksum = ttak_calc_header_checksum(header);

    if (global_trace_enabled) {
        header->tracking_log = malloc(1024);
        if (header->tracking_log) {
            snprintf(header->tracking_log, 1024,
                     "{\"event\":\"alloc\",\"ptr\":\"%p\",\"size\":%zu,\"ts\":%lu,\"root\":%d}",
                     (void*)((char*)header + header_size), size, now, (int)is_root);
            fprintf(stderr, "[MEM_TRACK] %s\n", header->tracking_log);
        }
    } else {
        header->tracking_log = NULL;
    }

    ttak_atomic_add64(&global_mem_usage, total_alloc_size);

    void *user_ptr = (char *)header + header_size;
    memset(user_ptr, 0, size);

    if (strict_check_enabled) {
        *((uint64_t *)((char *)user_ptr + size)) = TTAK_CANARY_END_MAGIC;
    }

    ensure_global_map(now);
    tt_map_t *map_handle = (tt_map_t *)global_ptr_map;
    if (global_init_done && !in_mem_init && !in_mem_op && map_handle) {
        if (is_root) {
            pthread_mutex_lock(&global_map_lock);
            in_mem_op = true;
            ttak_insert_to_map(map_handle, (uintptr_t)user_ptr, (size_t)header, now);
            ttak_mem_tree_add(&global_mem_tree, user_ptr, size, header->expires_tick, is_root);
            in_mem_op = false;
            pthread_mutex_unlock(&global_map_lock);
        }
    }

    return user_ptr;
}

/**
 * @brief Reallocate memory while preserving metadata and strict-check flags.
 *
 * @param ptr            Existing allocation (may be NULL).
 * @param new_size       Requested size.
 * @param lifetime_ticks Updated lifetime hint.
 * @param now            Current timestamp.
 * @param is_root        Whether the resulting allocation should be tracked as root.
 * @param flags          Allocation behavior flags.
 * @return Reallocated pointer or NULL on failure.
 */
void TTAK_HOT_PATH *ttak_mem_realloc_safe(void *ptr, size_t new_size, uint64_t lifetime_ticks, uint64_t now, _Bool is_root, ttak_mem_flags_t flags) {
    V_HEADER(ptr);
    if (!ptr) {
        return ttak_mem_alloc_safe(new_size, lifetime_ticks, now, false, false, true, is_root, flags);
    }

    ttak_mem_header_t *old_header = GET_HEADER(ptr);
    pthread_mutex_lock(&old_header->lock);
    bool is_const = old_header->is_const;
    bool is_volatile = old_header->is_volatile;
    bool allow_direct = old_header->allow_direct_access;
    size_t old_size = old_header->size;
    bool old_strict_check = old_header->strict_check; // Capture old strict_check
    pthread_mutex_unlock(&old_header->lock);

    // Pass the strict_check flag to the new allocation
    ttak_mem_flags_t new_flags = flags;
    if (old_strict_check) {
        new_flags |= TTAK_MEM_STRICT_CHECK;
    } else {
        new_flags &= ~TTAK_MEM_STRICT_CHECK;
    }

    void *new_ptr = ttak_mem_alloc_safe(new_size, lifetime_ticks, now, is_const, is_volatile, allow_direct, is_root, new_flags);
    if (!new_ptr) return NULL;

    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);

    ttak_mem_free(ptr);
    return new_ptr;
}

/**
 * @brief Duplicates a memory block while preserving metadata if source is tracked.
 *
 * @param src            Source memory block.
 * @param size           Number of bytes to copy.
 * @param lifetime_ticks Updated lifetime hint.
 * @param now            Current timestamp.
 * @param is_root        Whether the resulting allocation should be tracked as root.
 * @param flags          Allocation behavior flags.
 * @return Duplicated pointer or NULL on failure.
 */
void TTAK_HOT_PATH *ttak_mem_dup_safe(const void *src, size_t size, uint64_t lifetime_ticks, uint64_t now, _Bool is_root, ttak_mem_flags_t flags) {
    if (!src) return NULL;

    /* Check if src is a TTAK managed pointer to inherit flags if not overridden */
    ttak_mem_header_t *h_src = (ttak_mem_header_t *)src - 1;
    bool is_const = false;
    bool is_volatile = false;
    bool allow_direct = true;
    ttak_mem_flags_t final_flags = flags;

    /* Simple heuristic to check if it's likely a TTAK pointer */
    if (h_src->magic == TTAK_MAGIC_NUMBER) {
        is_const = h_src->is_const;
        is_volatile = h_src->is_volatile;
        allow_direct = h_src->allow_direct_access;
        if (h_src->strict_check) final_flags |= TTAK_MEM_STRICT_CHECK;
    }

    void *new_ptr = ttak_mem_alloc_safe(size, lifetime_ticks, now, is_const, is_volatile, allow_direct, is_root, final_flags);
    if (!new_ptr) return NULL;

#if defined(__TINYC__)
    /* TCC optimization: for very small sizes, manual copy might beat its memcpy */
    if (size <= 32) {
        const char *s = (const char *)src;
        char *d = (char *)new_ptr;
        for (size_t i = 0; i < size; i++) d[i] = s[i];
    } else {
        memcpy(new_ptr, src, size);
    }
#else
    memcpy(new_ptr, src, size);
#endif
    return new_ptr;
}

/**
 * @brief Free tracked memory, remove it from maps, and verify canaries.
 *
 * @param ptr Pointer returned by ttak_mem_alloc_safe.
 */
void TTAK_HOT_PATH ttak_mem_free(void *ptr) {
    volatile void *volatile_ptr = ptr;
    if (!volatile_ptr) return;
    void *stable_ptr = (void *)volatile_ptr;
    V_HEADER(stable_ptr); // This will check canaries if strict_check is enabled
    ttak_mem_header_t *header = GET_HEADER(stable_ptr);

    _Bool already_locked = (_Bool)in_mem_op;
    _Bool should_release_lock = false;
    if (header->is_root) {
        if (!already_locked) {
            pthread_mutex_lock(&global_map_lock);
            in_mem_op = 1;
            should_release_lock = true;
        }
        tt_map_t *map_handle = (tt_map_t *)global_ptr_map;
        if (map_handle) {
            ttak_delete_from_map(map_handle, (uintptr_t)stable_ptr, 0);
        }
        // Remove from heap tree
        ttak_mem_node_t *node_to_remove = ttak_mem_tree_find_node(&global_mem_tree, stable_ptr);
        if (node_to_remove) {
            ttak_mem_tree_remove(&global_mem_tree, node_to_remove);
        }
        if (should_release_lock) {
            in_mem_op = 0;
            pthread_mutex_unlock(&global_map_lock);
        }
    } else {
        // Optimistic free: if not root, we assume it's not in the tree (per optimized alloc).
        // But for safety against mixed usage, we *could* check, but to meet performance goals:
        // We assume consistent usage: if !is_root at alloc, it's not in tree.
        // So we do nothing here for mem_tree.
    }

    pthread_mutex_lock(&header->lock);
    if (header->freed) {
        pthread_mutex_unlock(&header->lock);
        return;
    }
    header->freed = true;

    if (header->tracking_log) {
        snprintf(header->tracking_log, 1024, "{\"event\":\"free\",\"ptr\":\"%p\",\"ts\":%lu}", stable_ptr, ttak_get_tick_count());
        fprintf(stderr, "[MEM_TRACK] %s\n", header->tracking_log);
        free(header->tracking_log);
        header->tracking_log = NULL;
    }

    size_t canary_padding = header->strict_check ? sizeof(uint64_t) : 0;
    size_t total_alloc_size = sizeof(ttak_mem_header_t) + canary_padding + header->size;

    // Check pin count for delayed free
    if (ttak_atomic_read64(&header->pin_count) > 0) {
        // In a real implementation, we would mark it for deferred cleanup.
        // For now, we'll proceed but this is where the Pinning mechanism would act.
    }
    pthread_mutex_unlock(&header->lock);

    ttak_atomic_sub64(&global_mem_usage, total_alloc_size);
    pthread_mutex_destroy(&header->lock);

    if (header->is_huge) {
#ifdef _WIN32
        VirtualFree(header, 0, MEM_RELEASE);
#else
        munmap(header, total_alloc_size);
#endif
    } else {
#ifdef _WIN32
        _aligned_free(header);
#else
        free(header);
#endif
    }
}

/**
 * @brief Configures the global background GC (mem_tree) parameters.
 */
void ttak_mem_configure_gc(uint64_t min_interval_ns, uint64_t max_interval_ns, size_t pressure_threshold) {
    uint64_t now = ttak_get_tick_count();
    ensure_global_map(now);
    ttak_mem_tree_set_cleaning_intervals(&global_mem_tree, min_interval_ns, max_interval_ns);
    ttak_mem_tree_set_pressure_threshold(&global_mem_tree, pressure_threshold);
}

/**
 * @brief Sweep and free expired or highly accessed allocations.
 *
 * @param now Current timestamp for expiration checks.
 */
void TTAK_COLD_PATH tt_autoclean_dirty_pointers(uint64_t now) {
    size_t count = 0;
    void **dirty = tt_inspect_dirty_pointers(now, &count);
    if (!dirty) return;
    for (size_t i = 0; i < count; i++) {
        volatile void *volatile_target = dirty[i];
        ttak_mem_free((void *)volatile_target);
    }
    free(dirty);
}

/**
 * @brief Return a snapshot of allocations considered "dirty".
 *
 * Caller owns the returned array.
 *
 * @param now       Current timestamp.
 * @param count_out Number of pointers returned.
 * @return Array of pointers or NULL if inspection fails.
 */
void TTAK_COLD_PATH **tt_inspect_dirty_pointers(uint64_t now, size_t *count_out) {
    tt_map_t *map_handle = (tt_map_t *)global_ptr_map;
    if (!count_out || !map_handle) return NULL;
    *count_out = 0;

    pthread_mutex_lock(&global_map_lock);
    size_t cap = map_handle->cap;
    void **dirty = malloc(sizeof(void *) * map_handle->size);
    if (!dirty) {
        pthread_mutex_unlock(&global_map_lock);
        return NULL;
    }
    size_t found = 0;
    for (size_t i = 0; i < cap; i++) {
        if (map_handle->tbl[i].ctrl == OCCUPIED) {
            void *user_ptr = (void *)map_handle->tbl[i].key;
            ttak_mem_header_t *h = (ttak_mem_header_t *)map_handle->tbl[i].value;
            if ((h->expires_tick != (uint64_t)-1 && now > h->expires_tick) ||
                ttak_atomic_read64(&h->access_count) > 1000000) {
                dirty[found++] = user_ptr;
            }
        }
    }
    pthread_mutex_unlock(&global_map_lock);
    *count_out = found;
    return dirty;
}

/**
 * @brief Run the auto-clean pass and then report dirty pointers.
 *
 * @param now       Current timestamp.
 * @param count_out Populated with the number of dirty pointers.
 * @return Array of dirty pointers (caller frees) or NULL.
 */
void **tt_autoclean_and_inspect(uint64_t now, size_t *count_out) {
    tt_autoclean_dirty_pointers(now);
    return tt_inspect_dirty_pointers(now, count_out);
}

/**
 * @brief "Conservative Mode" pressure sensor for internal schedulers.
 */
_Bool ttak_mem_is_pressure_high(void) {
    return ttak_atomic_read64(&global_mem_usage) > TTAK_MEM_HIGH_WATERMARK;
}

/**
 * @brief Atomic WAL pattern for secure state persistence.
 */
void save_current_progress(const char *filename, const void *data, size_t size) {
    char temp_name[256];
    snprintf(temp_name, sizeof(temp_name), "%s.tmp", filename);

    int fd = open(temp_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;

    if (write(fd, data, size) != (ssize_t)size) {
        close(fd);
        unlink(temp_name);
        return;
    }

    if (fsync(fd) != 0) {
        close(fd);
        unlink(temp_name);
        return;
    }

    close(fd);

    if (rename(temp_name, filename) != 0) {
        unlink(temp_name);
        return;
    }

#ifndef _WIN32
    // Sync parent directory
    int dfd = open(".", O_RDONLY | O_DIRECTORY);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }
#endif
}
