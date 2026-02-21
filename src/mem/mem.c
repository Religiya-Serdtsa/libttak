/**
 * @file mem.c
 * @brief Implementation of the TTAK Unified Memory Subsystem.
 *
 * Implements tiered memory allocation, lifecycle tracking, and friction-based
 * pressure sensing for high-performance concurrent applications.
 */

#include <ttak/mem/mem.h>
#include <ttak/atomic/atomic.h>
#include <ttak/ht/map.h>
#include <ttak/timing/timing.h>
#include <ttak/mem_tree/mem_tree.h>
#include "../../internal/app_types.h"
#include "../../internal/ttak/mem_internal.h"

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <vm/uma.h> // For UMA_ALIGN_PTR
#include <stdarg.h> // For printf with va_list

#ifndef ENOMEM
#define ENOMEM 12 // Dummy value for kernel, actual errno not used directly
#endif

// Kernel memory allocation wrappers
#define kmem_alloc(size, flags)		malloc(size, M_TEMP, (flags))
#define kmem_zalloc(size, flags)	malloc(size, M_TEMP, (flags) | M_ZERO)
#define kmem_free(ptr, flags)		free(ptr, M_TEMP)
#define kmem_realloc(ptr, size, flags)	realloc(ptr, size, M_TEMP, (flags))
#define kmem_calloc(count, size, flags)	malloc(count * size, M_TEMP, (flags) | M_ZERO)


static __inline int
posix_memalign_k(void **memptr, size_t alignment, size_t size) {
    // In kernel, kmem_alloc is typically page-aligned. If more strict alignment
    // is needed, uma_zalloc_smr_align() might be better, or custom alignment logic.
    // For now, assuming kmem_alloc provides sufficient alignment for general use.
    // M_WAITOK implies it can sleep.
    *memptr = kmem_alloc(size, M_WAITOK | M_ZERO);
    return (*memptr) ? 0 : ENOMEM;
}
#define posix_memalign posix_memalign_k
#define posix_memfree(ptr) kmem_free(ptr, M_TEMP) // Use kmem_free for posix_memfree


#define fprintf(file, fmt, ...) printf(fmt, ##__VA_ARGS__) // Redirect fprintf to printf
#define stderr (void *)NULL // Dummy stderr
#define abort() panic("TTAK fatal error at %s:%d", __FILE__, __LINE__) // Replace abort with panic

// Replace pthread with kernel mutex
#define pthread_mutex_t mtx_t
#define pthread_mutex_init(mtx, attr) mtx_init(mtx, "ttak_mtx", NULL, MTX_DEF)
#define pthread_mutex_lock(mtx) mtx_lock(mtx)
#define pthread_mutex_unlock(mtx) mtx_unlock(mtx)
#define pthread_mutex_destroy(mtx) mtx_destroy(mtx)
// For pthread_once_t, use a simple boolean guard with a mutex
// Define these globally, not inside a function.
#define PTHREAD_ONCE_INIT {0} // Dummy, will be replaced by direct init

#define MAP_HUGETLB 0x0 // No huge pages via mmap in kernel context yet
#define MAP_FAILED ((void *)-1) // Dummy value

#else // ! _KERNEL
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include <errno.h>

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

#endif // _KERNEL

// Common includes and definitions
#include <stdalign.h>
#include <stdbool.h>
#ifndef _KERNEL
#include <stdio.h>
#include <fcntl.h>
#endif
#include <string.h> // Ensure string.h is included for both kernel and userland
#ifndef _KERNEL
#include <limits.h>
#include <errno.h>
#endif


/**
 * @brief Internal canary magic numbers for boundary check validation.
 */
#define TTAK_CANARY_START_MAGIC 0xDEADBEEFDEADBEEFULL
#define TTAK_CANARY_END_MAGIC   0xBEEFDEADBEEFDEADULL

static volatile uint64_t global_mem_usage = 0;           /**< Atomic counter for total libttak usage */
static mtx_t global_map_lock; /**< Global lock for pointer map */
static ttak_mem_tree_t global_mem_tree;                 /**< Global root-tracking mem_tree */
static int global_trace_enabled = 0;                    /**< Flag for JSON tracing */

/**
 * @brief Initialize the global friction matrix for Damping.
 */
ttak_mem_friction_matrix_t global_friction_matrix = {
    .values = { ATOMIC_VAR_INIT(TTAK_FP_ONE), ATOMIC_VAR_INIT(TTAK_FP_ONE), ATOMIC_VAR_INIT(TTAK_FP_ONE), ATOMIC_VAR_INIT(TTAK_FP_ONE) },
    .global_friction = ATOMIC_VAR_INIT(TTAK_FP_ONE),
    .pressure_threshold = TTAK_FP_FROM_INT(1)
};

/**
 * @brief Internal validation macro for header integrity and canaries.
 */
#define V_HEADER(ptr) do { \
    if (!ptr) break; \
    ttak_mem_header_t *_h = (ttak_mem_header_t *)(ptr) - 1; \
    if (_h->magic != TTAK_MAGIC_NUMBER || _h->checksum != ttak_calc_header_checksum(_h)) { \
        printf("[FATAL] TTAK Memory Corruption detected at %p (Header corrupted) %s:%d\n", (void*)ptr, __FILE__, __LINE__); \
        panic("TTAK Memory Corruption detected"); \
    } \
    if (_h->strict_check) { \
        if (_h->canary_start != TTAK_CANARY_START_MAGIC) { \
            printf("[FATAL] TTAK Memory Corruption detected at %p (Start canary corrupted in header) %s:%d\n", (void*)ptr, __FILE__, __LINE__); \
            panic("TTAK Memory Corruption detected"); \
        } \
        uint64_t *canary_end_ptr = (uint64_t *)((char *)ptr + _h->size); \
        if (*canary_end_ptr != TTAK_CANARY_END_MAGIC) { \
            printf("[FATAL] TTAK Memory Corruption detected at %p (End canary corrupted) %s:%d\n", (void*)ptr, __FILE__, __LINE__); \
            panic("TTAK Memory Corruption detected"); \
        } \
    } \
} while(0)

#define GET_HEADER(ptr) ((ttak_mem_header_t *)(ptr) - 1)
#define GET_USER_PTR(header) ((void *)((ttak_mem_header_t *)(header) + 1))

static volatile tt_map_t *global_ptr_map = NULL;
static mtx_t global_init_lock;

#if EMBEDDED
#include <ttak/phys/mem/buddy.h>
static TTAK_THREAD_LOCAL uint8_t buddy_pool[1 << 20];
static bool buddy_initialized = false;
/**
 * @brief Lazy-init for the buddy system in embedded builds.
 */
static void buddy_bootstrap(void) {
    if (!buddy_initialized) {
        ttak_mem_buddy_init(buddy_pool, sizeof(buddy_pool), 1);
        buddy_initialized = true;
    }
}
#endif

TTAK_THREAD_LOCAL bool t_reentrancy_guard = false;  /**< Thread-local guard against recursive allocation */
TTAK_THREAD_LOCAL int in_mem_op = 0;              /**< Guard for pointer-map operations */
TTAK_THREAD_LOCAL int in_mem_init = 0;            /**< Guard for subsystem initialization */
static volatile int global_init_done = 0;         /**< Subsystem initialization ready flag */

/**
 * @brief Recalculates the global friction as the product of all class values.
 */
static ttak_fixed_16_16_t ttak_mem_calculate_global_friction(void) {
    ttak_fixed_16_16_t friction_product = TTAK_FP_ONE;
    for (int i = 0; i < 4; ++i) {
        friction_product = TTAK_FP_MUL(friction_product, atomic_load(&global_friction_matrix.values[i]));
    }
    atomic_store(&global_friction_matrix.global_friction, friction_product);
    return friction_product;
}

/**
 * @brief Updates a friction value using Exponential Weighted Moving Average (EWMA).
 */
static void ttak_mem_update_friction_value(int size_class_idx, bool waste_detected) {
    if (size_class_idx < 0 || size_class_idx >= 4) return;
    ttak_fixed_16_16_t alpha = TTAK_FP_FROM_INT(1) / 4; 
    ttak_fixed_16_16_t one_minus_alpha = TTAK_FP_FROM_INT(1) - alpha;
    ttak_fixed_16_16_t old_value = atomic_load(&global_friction_matrix.values[size_class_idx]);
    ttak_fixed_16_16_t new_target = waste_detected ? TTAK_FP_FROM_INT(2) : TTAK_FP_FROM_INT(1);
    ttak_fixed_16_16_t new_value = TTAK_FP_MUL(old_value, one_minus_alpha) + TTAK_FP_MUL(new_target, alpha);
    atomic_store(&global_friction_matrix.values[size_class_idx], new_value);
    ttak_mem_calculate_global_friction();
}

/**
 * @brief Ensures the global pointer map and mem_tree are initialized.
 */
static void ensure_global_map(uint64_t now) {
    if (global_init_done || in_mem_init) return;
    mtx_lock(&global_init_lock); // Use kernel mutex
    if (!global_ptr_map && !global_init_done) {
        in_mem_init = true;
        mtx_init(&global_map_lock, "ttak_gmap_mtx", NULL, MTX_DEF); // Initialize global_map_lock
        global_ptr_map = ttak_create_map(8192, now);
        ttak_mem_tree_init(&global_mem_tree);
        global_init_done = true;
        in_mem_init = false;
    }
    mtx_unlock(&global_init_lock); // Use kernel mutex
}

// See public interfaces in include/ttak/mem/mem.h for full documentation

void TTAK_HOT_PATH *ttak_mem_alloc_safe(size_t size, uint64_t lifetime_ticks, uint64_t now, _Bool is_const, _Bool is_volatile, _Bool allow_direct, _Bool is_root, ttak_mem_flags_t flags) {
    size_t header_size = sizeof(ttak_mem_header_t);
    bool strict_check_enabled = (flags & TTAK_MEM_STRICT_CHECK);
    ttak_mem_header_t *header = NULL;
    ttak_allocation_tier_t allocated_tier = TTAK_ALLOC_TIER_UNKNOWN;
    void *user_ptr = NULL;

    if (t_reentrancy_guard) {
        // Fallback to kmem_alloc if reentrancy detected in kernel.
        // This is a simplified fallback; a robust solution might need a dedicated zone.
        void* fallback_ptr = kmem_zalloc(size, M_NOWAIT);
        return fallback_ptr;
    }
    t_reentrancy_guard = true;

    // --- Tier 1: Pockets ---
    // Placeholder, needs kernel implementation of pocket allocator
    if (size > 0 && size <= 128) {
        // header = ttak_mem_pocket_alloc_internal(size);
        // if (header) allocated_tier = TTAK_ALLOC_TIER_POCKET;
    }

    // --- Tier 2: VMA ---
    // Placeholder, needs kernel implementation of VMA allocator
    if (!header && size > 0 && size <= 16384) {
        // header = ttak_mem_vma_alloc_internal(size);
        // if (header) allocated_tier = TTAK_ALLOC_TIER_VMA;
    }

    // --- Tier 3: General/Buddy ---
    if (!header) {
#if EMBEDDED
        if ((flags & TTAK_MEM_LOW_PRIORITY) &&
            atomic_load(&global_friction_matrix.global_friction) > atomic_load(&global_friction_matrix.pressure_threshold)) {
            t_reentrancy_guard = false;
            return NULL;
        }
        // pthread_once(&buddy_once, buddy_bootstrap); // Replaced with direct call + guard
        buddy_bootstrap();
        ttak_mem_req_t req = { .size_bytes = size + sizeof(ttak_mem_header_t), .priority = 1, .owner_tag = 0, .call_safety = 0, .flags = 0 };
        header = ttak_mem_buddy_alloc(&req);
        if (header) {
            allocated_tier = TTAK_ALLOC_TIER_BUDDY;
            strict_check_enabled = false;
        }
#else // Not EMBEDDED
        size_t canary_padding = strict_check_enabled ? sizeof(uint64_t) : 0;
        size_t total_alloc_size = header_size + canary_padding + size;
        
        // Remove huge pages logic for kernel, use standard kmem_alloc
        // if (flags & TTAK_MEM_HUGE_PAGES) { ... }

        if (posix_memalign((void **)&header, 64, total_alloc_size) != 0) header = NULL;
        else allocated_tier = TTAK_ALLOC_TIER_GENERAL;
        
        // Removed errno handling, replaced with direct fallback
        if (!header) {
            static TTAK_THREAD_LOCAL int retrying = 0;
            if (!retrying) {
                retrying = 1; t_reentrancy_guard = false;
                tt_autoclean_dirty_pointers(now);
                t_reentrancy_guard = true;
                void *res = ttak_mem_alloc_safe(size, lifetime_ticks, now, is_const, is_volatile, allow_direct, is_root, flags);
                retrying = 0; t_reentrancy_guard = false;
                return res;
            }
        }
#endif
    }

    if (!header) { t_reentrancy_guard = false; return NULL; }

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
    header->is_huge = (allocated_tier == TTAK_ALLOC_TIER_GENERAL && (flags & TTAK_MEM_HUGE_PAGES));
    header->should_join = false;
    header->strict_check = strict_check_enabled;
    header->is_root = is_root;
    header->canary_start = strict_check_enabled ? TTAK_CANARY_START_MAGIC : 0;
    header->canary_end = strict_check_enabled ? TTAK_CANARY_END_MAGIC : 0;
    mtx_init(&header->lock, "ttak_hdr_mtx", NULL, MTX_DEF); // Use mtx_init
    header->allocation_tier = allocated_tier;
    header->checksum = ttak_calc_header_checksum(header);

    size_t actual_total_alloc_size;
    if (allocated_tier == TTAK_ALLOC_TIER_POCKET) actual_total_alloc_size = get_total_block_size_for_freelist(get_pocket_size_class_idx(header_size + size));
    else if (allocated_tier == TTAK_ALLOC_TIER_VMA) actual_total_alloc_size = (header_size + size + TTAK_VMA_ALIGNMENT - 1) & ~((size_t)TTAK_VMA_ALIGNMENT - 1);
    else actual_total_alloc_size = header_size + size + (strict_check_enabled ? sizeof(uint64_t) : 0);

    ttak_atomic_add64(&global_mem_usage, actual_total_alloc_size);
    user_ptr = (char *)header + header_size;
    memset(user_ptr, 0, size);
    if (strict_check_enabled) *((uint64_t *)((char *)user_ptr + size)) = TTAK_CANARY_END_MAGIC;

    if (global_trace_enabled) {
        header->tracking_log = kmem_alloc(1024, M_NOWAIT); // Uses kmem_alloc
        if (header->tracking_log) {
            snprintf(header->tracking_log, 1024, "{\"event\":\"alloc\",\"ptr\":\"%p\",\"size\":%zu,\"ts\":%lu,\"root\":%d,\"tier\":%d}", user_ptr, size, now, (int)is_root, (int)allocated_tier);
            printf("[MEM_TRACK] %s\n", header->tracking_log); // Use printf
        }
    } else header->tracking_log = NULL;

    if (is_root) {
        ensure_global_map(now);
        tt_map_t *map_handle = (tt_map_t *)global_ptr_map;
        if (global_init_done && !in_mem_init && !in_mem_op && map_handle) {
            mtx_lock(&global_map_lock); in_mem_op = true; // Use kernel mutex
            ttak_insert_to_map(map_handle, (uintptr_t)user_ptr, (size_t)header, now);
            ttak_mem_tree_add(&global_mem_tree, user_ptr, size, header->expires_tick, is_root);
            in_mem_op = false; mtx_unlock(&global_map_lock); // Use kernel mutex
        }
    }

    t_reentrancy_guard = false;
    return user_ptr;
}

void TTAK_HOT_PATH *ttak_mem_realloc_safe(void *ptr, size_t new_size, uint64_t lifetime_ticks, uint64_t now, _Bool is_root, ttak_mem_flags_t flags) {
    V_HEADER(ptr);
    if (!ptr) return ttak_mem_alloc_safe(new_size, lifetime_ticks, now, false, false, true, is_root, flags);

    ttak_mem_header_t *old_header = GET_HEADER(ptr);
    mtx_lock(&old_header->lock); // Use kernel mutex
    bool is_const = old_header->is_const, is_volatile = old_header->is_volatile, allow_direct = old_header->allow_direct_access, old_strict = old_header->strict_check;
    size_t old_size = old_header->size;
    mtx_unlock(&old_header->lock); // Use kernel mutex

    ttak_mem_flags_t new_flags = flags;
    if (old_strict) new_flags |= TTAK_MEM_STRICT_CHECK; else new_flags &= ~TTAK_MEM_STRICT_CHECK;

    void *new_ptr = ttak_mem_alloc_safe(new_size, lifetime_ticks, now, is_const, is_volatile, allow_direct, is_root, new_flags);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, (old_size < new_size) ? old_size : new_size);
    ttak_mem_free(ptr);
    return new_ptr;
}

void TTAK_HOT_PATH *ttak_mem_dup_safe(const void *src, size_t size, uint64_t lifetime_ticks, uint64_t now, _Bool is_root, ttak_mem_flags_t flags) {
    if (!src) return NULL;
    ttak_mem_header_t *h_src = (ttak_mem_header_t *)src - 1;
    bool is_const = false, is_volatile = false, allow_direct = true;
    ttak_mem_flags_t final_flags = flags;
    if (h_src->magic == TTAK_MAGIC_NUMBER) {
        is_const = h_src->is_const; is_volatile = h_src->is_volatile; allow_direct = h_src->allow_direct_access;
        if (h_src->strict_check) final_flags |= TTAK_MEM_STRICT_CHECK;
    }
    void *new_ptr = ttak_mem_alloc_safe(size, lifetime_ticks, now, is_const, is_volatile, allow_direct, is_root, final_flags);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, src, size);
    return new_ptr;
}

void TTAK_HOT_PATH ttak_mem_free(void *ptr) {
    if (!ptr) return;
    void *stable_ptr = ptr;
    ttak_mem_header_t *header = GET_HEADER(stable_ptr);

    mtx_lock(&header->lock); // Use kernel mutex
    if (header->freed) { mtx_unlock(&header->lock); return; } // Use kernel mutex
    header->freed = true;
    mtx_unlock(&header->lock); // Use kernel mutex

    V_HEADER(stable_ptr);

    if (global_trace_enabled && header->tracking_log) {
        snprintf(header->tracking_log, 1024, "{\"event\":\"free\",\"ptr\":\"%p\",\"ts\":%lu,\"tier\":%d}", stable_ptr, ttak_get_tick_count(), (int)header->allocation_tier);
        printf("[MEM_TRACK] %s\n", header->tracking_log); // Use printf
        kmem_free(header->tracking_log, M_TEMP); header->tracking_log = NULL; // Uses kmem_free
    }

    size_t header_size = sizeof(ttak_mem_header_t);
    size_t actual_total_alloc_size;
    bool strict_check_enabled = header->strict_check;

    switch (header->allocation_tier) {
        case TTAK_ALLOC_TIER_POCKET: actual_total_alloc_size = get_total_block_size_for_freelist(get_pocket_size_class_idx(header_size + header->size)); break; // Needs kernel implementation
        case TTAK_ALLOC_TIER_VMA: actual_total_alloc_size = (header_size + header->size + TTAK_VMA_ALIGNMENT - 1) & ~((size_t)TTAK_VMA_ALIGNMENT - 1); break; // Needs kernel implementation
        case TTAK_ALLOC_TIER_BUDDY: actual_total_alloc_size = header_size + header->size; break;
        case TTAK_ALLOC_TIER_GENERAL: actual_total_alloc_size = header_size + header->size + (strict_check_enabled ? sizeof(uint64_t) : 0); break;
        default: actual_total_alloc_size = header_size + header->size + (strict_check_enabled ? sizeof(uint64_t) : 0); break;
    }

    if (header->is_root && (header->allocation_tier == TTAK_ALLOC_TIER_GENERAL || header->allocation_tier == TTAK_ALLOC_TIER_BUDDY)) {
        mtx_lock(&global_map_lock); in_mem_op = 1; // Use kernel mutex
        ttak_delete_from_map((tt_map_t*)global_ptr_map, (uintptr_t)stable_ptr, 0);
        ttak_mem_node_t *node = ttak_mem_tree_find_node(&global_mem_tree, stable_ptr);
        if (node) ttak_mem_tree_remove(&global_mem_tree, node);
        in_mem_op = 0; mtx_unlock(&global_map_lock); // Use kernel mutex
    }

    ttak_atomic_sub64(&global_mem_usage, actual_total_alloc_size);

    switch (header->allocation_tier) {
        case TTAK_ALLOC_TIER_POCKET: _pocket_free_internal(header); break; // Needs kernel implementation
        case TTAK_ALLOC_TIER_VMA: _vma_free_internal(header); break; // Needs kernel implementation
        case TTAK_ALLOC_TIER_BUDDY:
#if EMBEDDED
            ttak_mem_buddy_free(header);
#else
            mtx_destroy(&header->lock); kmem_free(header, M_TEMP); // Use kernel mutex and free
#endif
            break;
        case TTAK_ALLOC_TIER_GENERAL:
            mtx_destroy(&header->lock); // Use kernel mutex
            // Remove huge pages logic for kernel, use standard kmem_free
            // if (header->is_huge) VirtualFree(header, 0, MEM_RELEASE); else _aligned_free(header);
            kmem_free(header, M_TEMP); // Use kernel free
            break;
        default:
            mtx_destroy(&header->lock); // Use kernel mutex
#if EMBEDDED
            ttak_mem_buddy_free(header);
#else
            // if (header->is_huge) munmap(header, actual_total_alloc_size); else free(header);
            kmem_free(header, M_TEMP); // Use kernel free
#endif
            break;
    }
}

void ttak_mem_set_trace(int enable) {
    global_trace_enabled = enable;
    if (!global_init_done) return;
    mtx_lock(&global_map_lock); // Use kernel mutex
    tt_map_t *map_handle = (tt_map_t *)global_ptr_map;
    if (map_handle) {
        for (size_t i = 0; i < map_handle->cap; i++) {
            if (map_handle->tbl[i].ctrl == OCCUPIED) {
                ttak_mem_header_t *h = (ttak_mem_header_t *)map_handle->tbl[i].value;
                mtx_lock(&h->lock); // Use kernel mutex
                if (enable && !h->tracking_log) {
                    h->tracking_log = kmem_alloc(1024, M_NOWAIT); // Uses kmem_alloc
                    if (h->tracking_log) snprintf(h->tracking_log, 1024, "{\"event\":\"trace_enabled\",\"ts\":%lu}", ttak_get_tick_count());
                } else if (!enable && h->tracking_log) { kmem_free(h->tracking_log, M_TEMP); h->tracking_log = NULL; } // Uses kmem_free
                mtx_unlock(&h->lock); // Use kernel mutex
            }
        }
    }
    mtx_unlock(&global_map_lock); // Use kernel mutex
}

int ttak_mem_is_trace_enabled(void) { return global_trace_enabled; }

void ttak_mem_configure_gc(uint64_t min_interval_ns, uint64_t max_interval_ns, size_t pressure_threshold) {
    uint64_t now = ttak_get_tick_count();
    ensure_global_map(now);
    ttak_mem_tree_set_cleaning_intervals(&global_mem_tree, min_interval_ns, max_interval_ns);
    ttak_mem_tree_set_pressure_threshold(&global_mem_tree, pressure_threshold);
}

void TTAK_COLD_PATH tt_autoclean_dirty_pointers(uint64_t now) {
    size_t count = 0; void **dirty = tt_inspect_dirty_pointers(now, &count);
    if (!dirty) return;
    for (size_t i = 0; i < count; i++) kmem_free(dirty[i], M_TEMP); // uses kmem_free
    kmem_free(dirty, M_TEMP); // Uses kmem_free
}

void TTAK_COLD_PATH **tt_inspect_dirty_pointers(uint64_t now, size_t *count_out) {
    tt_map_t *map_handle = (tt_map_t *)global_ptr_map;
    if (!count_out || !map_handle) return NULL;
    mtx_lock(&global_map_lock); in_mem_op = 1; // Use kernel mutex
    void **dirty = kmem_alloc(sizeof(void *) * map_handle->size, M_NOWAIT); // Uses kmem_alloc
    if (!dirty) { mtx_unlock(&global_map_lock); return NULL; } // Use kernel mutex
    size_t found = 0;
    for (size_t i = 0; i < map_handle->cap; i++) {
        if (map_handle->tbl[i].ctrl == OCCUPIED) {
            ttak_mem_header_t *h = (ttak_mem_header_t *)map_handle->tbl[i].value;
            if ((h->expires_tick != (uint64_t)-1 && now > h->expires_tick) || ttak_atomic_read64(&h->access_count) > 1000000)
                dirty[found++] = (void*)map_handle->tbl[i].key;
        }
    }
    mtx_unlock(&global_map_lock); *count_out = found; return dirty; // Use kernel mutex
}

void **tt_autoclean_and_inspect(uint64_t now, size_t *count_out) {
    tt_autoclean_dirty_pointers(now); return tt_inspect_dirty_pointers(now, count_out);
}

_Bool ttak_mem_is_pressure_high(void) { return ttak_atomic_read64(&global_mem_usage) > TTAK_MEM_HIGH_WATERMARK; }

void save_current_progress(const char *filename, const void *data, size_t size) {
    // This function performs file I/O, which is not suitable for the kernel.
    // Conditionalize out or replace with kernel logging/dumping mechanism if needed.
#ifndef _KERNEL
    char temp_name[256]; snprintf(temp_name, sizeof(temp_name), "%s.tmp", filename);
    int fd = open(temp_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    if (write(fd, data, size) != (ssize_t)size) { close(fd); unlink(temp_name); return; }
    if (fsync(fd) != 0) { close(fd); unlink(temp_name); return; }
    close(fd); if (rename(temp_name, filename) != 0) unlink(temp_name);
#ifndef _WIN32
    int dfd = open(".", O_RDONLY | O_DIRECTORY); if (dfd >= 0) { fsync(dfd); close(dfd); }
#endif
#else
    // In kernel, we might log to dmesg or a debug buffer.
    printf("TTAK: save_current_progress called in kernel for %s (size %zu). Not implemented.\n", filename, size);
#endif
}

#if EMBEDDED
void ttak_mem_set_embedded_pool(void *pool_start, size_t pool_len) {
    if (!pool_start || pool_len == 0) return;
    // pthread_once(&buddy_once, buddy_bootstrap); // Replaced with direct call + guard
    buddy_bootstrap();
    ttak_mem_buddy_set_pool(pool_start, pool_len);
}
#endif
