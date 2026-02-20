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
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <limits.h>
#include <errno.h>

#ifdef _WIN32
    #define _CRT_NONSTDC_NO_DEPRECATE 1
    #include <windows.h>
    #include <io.h>
    #define fsync(fd) _commit(fd)
    #define unlink _unlink
    typedef SSIZE_T ssize_t;  /* POSIX ssize_t for MSVC */
    /* Map POSIX CRT names to their MSVC underscore equivalents */
    #ifndef open
    #  define open  _open
    #endif
    #ifndef write
    #  define write(fd, buf, n) _write((fd), (buf), (unsigned int)(n))
    #endif
    #ifndef close
    #  define close _close
    #endif
    #ifndef read
    #  define read  _read
    #endif
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

/**
 * @brief Internal canary magic numbers for boundary check validation.
 */
#define TTAK_CANARY_START_MAGIC 0xDEADBEEFDEADBEEFULL
#define TTAK_CANARY_END_MAGIC   0xBEEFDEADBEEFDEADULL

static volatile uint64_t global_mem_usage = 0;           /**< Atomic counter for total libttak usage */
static pthread_mutex_t global_map_lock = PTHREAD_MUTEX_INITIALIZER; /**< Global lock for pointer map */
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

#if EMBEDDED
#include <ttak/phys/mem/buddy.h>
static TTAK_THREAD_LOCAL uint8_t buddy_pool[1 << 20];
static pthread_once_t buddy_once = PTHREAD_ONCE_INIT;
/**
 * @brief Lazy-init for the buddy system in embedded builds.
 */
static void buddy_bootstrap(void) {
    ttak_mem_buddy_init(buddy_pool, sizeof(buddy_pool), 1);
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
    pthread_mutex_lock(&global_init_lock);
    if (!global_ptr_map && !global_init_done) {
        in_mem_init = true;
        global_ptr_map = ttak_create_map(8192, now);
        ttak_mem_tree_init(&global_mem_tree);
        global_init_done = true;
        in_mem_init = false;
    }
    pthread_mutex_unlock(&global_init_lock);
}

// See public interfaces in include/ttak/mem/mem.h for full documentation

void TTAK_HOT_PATH *ttak_mem_alloc_safe(size_t size, uint64_t lifetime_ticks, uint64_t now, _Bool is_const, _Bool is_volatile, _Bool allow_direct, _Bool is_root, ttak_mem_flags_t flags) {
    size_t header_size = sizeof(ttak_mem_header_t);
    bool strict_check_enabled = (flags & TTAK_MEM_STRICT_CHECK);
    ttak_mem_header_t *header = NULL;
    ttak_allocation_tier_t allocated_tier = TTAK_ALLOC_TIER_UNKNOWN;
    void *user_ptr = NULL;

    if (t_reentrancy_guard) {
        void* fallback_ptr = malloc(size);
        if (fallback_ptr) memset(fallback_ptr, 0, size);
        return fallback_ptr;
    }
    t_reentrancy_guard = true;

    // --- Tier 1: Pockets ---
    if (size > 0 && size <= 128) {
        header = ttak_mem_pocket_alloc_internal(size);
        if (header) allocated_tier = TTAK_ALLOC_TIER_POCKET;
    }

    // --- Tier 2: VMA ---
    if (!header && size > 0 && size <= 16384) {
        header = ttak_mem_vma_alloc_internal(size);
        if (header) allocated_tier = TTAK_ALLOC_TIER_VMA;
    }

    // --- Tier 3: General/Buddy ---
    if (!header) {
#if EMBEDDED
        if ((flags & TTAK_MEM_LOW_PRIORITY) &&
            atomic_load(&global_friction_matrix.global_friction) > atomic_load(&global_friction_matrix.pressure_threshold)) {
            t_reentrancy_guard = false;
            return NULL;
        }
        pthread_once(&buddy_once, buddy_bootstrap);
        ttak_mem_req_t req = { .size_bytes = size + sizeof(ttak_mem_header_t), .priority = 1, .owner_tag = 0, .call_safety = 0, .flags = 0 };
        header = ttak_mem_buddy_alloc(&req);
        if (header) {
            allocated_tier = TTAK_ALLOC_TIER_BUDDY;
            strict_check_enabled = false;
        }
#else
        size_t canary_padding = strict_check_enabled ? sizeof(uint64_t) : 0;
        size_t total_alloc_size = header_size + canary_padding + size;
        if (flags & TTAK_MEM_HUGE_PAGES) {
#ifdef _WIN32
            header = VirtualAlloc(NULL, total_alloc_size, MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES, PAGE_READWRITE);
#else
            header = mmap(NULL, total_alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
#endif
            if (header != MAP_FAILED) allocated_tier = TTAK_ALLOC_TIER_GENERAL;
            else header = NULL;
        }
        if (!header) {
            if (posix_memalign((void **)&header, 64, total_alloc_size) != 0) header = NULL;
            else allocated_tier = TTAK_ALLOC_TIER_GENERAL;
        }
        if (!header && errno == ENOMEM) {
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
    pthread_mutex_init(&header->lock, NULL);
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
        header->tracking_log = malloc(1024);
        if (header->tracking_log) {
            snprintf(header->tracking_log, 1024, "{\"event\":\"alloc\",\"ptr\":\"%p\",\"size\":%zu,\"ts\":%" PRIu64 ",\"root\":%d,\"tier\":%d}", user_ptr, size, now, (int)is_root, (int)allocated_tier);
            fprintf(stderr, "[MEM_TRACK] %s\n", header->tracking_log);
        }
    } else header->tracking_log = NULL;

    if (is_root) {
        ensure_global_map(now);
        tt_map_t *map_handle = (tt_map_t *)global_ptr_map;
        if (global_init_done && !in_mem_init && !in_mem_op && map_handle) {
            pthread_mutex_lock(&global_map_lock); in_mem_op = true;
            ttak_insert_to_map(map_handle, (uintptr_t)user_ptr, (size_t)header, now);
            ttak_mem_tree_add(&global_mem_tree, user_ptr, size, header->expires_tick, is_root);
            in_mem_op = false; pthread_mutex_unlock(&global_map_lock);
        }
    }

    t_reentrancy_guard = false;
    return user_ptr;
}

void TTAK_HOT_PATH *ttak_mem_realloc_safe(void *ptr, size_t new_size, uint64_t lifetime_ticks, uint64_t now, _Bool is_root, ttak_mem_flags_t flags) {
    V_HEADER(ptr);
    if (!ptr) return ttak_mem_alloc_safe(new_size, lifetime_ticks, now, false, false, true, is_root, flags);

    ttak_mem_header_t *old_header = GET_HEADER(ptr);
    pthread_mutex_lock(&old_header->lock);
    bool is_const = old_header->is_const, is_volatile = old_header->is_volatile, allow_direct = old_header->allow_direct_access, old_strict = old_header->strict_check;
    size_t old_size = old_header->size;
    pthread_mutex_unlock(&old_header->lock);

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

    pthread_mutex_lock(&header->lock);
    if (header->freed) { pthread_mutex_unlock(&header->lock); return; }
    header->freed = true;
    pthread_mutex_unlock(&header->lock);

    V_HEADER(stable_ptr);

    if (global_trace_enabled && header->tracking_log) {
        snprintf(header->tracking_log, 1024, "{\"event\":\"free\",\"ptr\":\"%p\",\"ts\":%" PRIu64 ",\"tier\":%d}", stable_ptr, ttak_get_tick_count(), (int)header->allocation_tier);
        fprintf(stderr, "[MEM_TRACK] %s\n", header->tracking_log);
        free(header->tracking_log); header->tracking_log = NULL;
    }

    size_t header_size = sizeof(ttak_mem_header_t);
    size_t actual_total_alloc_size;
    bool strict_check_enabled = header->strict_check;

    switch (header->allocation_tier) {
        case TTAK_ALLOC_TIER_POCKET: actual_total_alloc_size = get_total_block_size_for_freelist(get_pocket_size_class_idx(header_size + header->size)); break;
        case TTAK_ALLOC_TIER_VMA: actual_total_alloc_size = (header_size + header->size + TTAK_VMA_ALIGNMENT - 1) & ~((size_t)TTAK_VMA_ALIGNMENT - 1); break;
        case TTAK_ALLOC_TIER_BUDDY: actual_total_alloc_size = header_size + header->size; break;
        case TTAK_ALLOC_TIER_GENERAL: actual_total_alloc_size = header_size + header->size + (strict_check_enabled ? sizeof(uint64_t) : 0); break;
        default: actual_total_alloc_size = header_size + header->size + (strict_check_enabled ? sizeof(uint64_t) : 0); break;
    }

    if (header->is_root && (header->allocation_tier == TTAK_ALLOC_TIER_GENERAL || header->allocation_tier == TTAK_ALLOC_TIER_BUDDY)) {
        pthread_mutex_lock(&global_map_lock); in_mem_op = 1;
        ttak_delete_from_map((tt_map_t*)global_ptr_map, (uintptr_t)stable_ptr, 0);
        ttak_mem_node_t *node = ttak_mem_tree_find_node(&global_mem_tree, stable_ptr);
        if (node) ttak_mem_tree_remove(&global_mem_tree, node);
        in_mem_op = 0; pthread_mutex_unlock(&global_map_lock);
    }

    ttak_atomic_sub64(&global_mem_usage, actual_total_alloc_size);

    switch (header->allocation_tier) {
        case TTAK_ALLOC_TIER_POCKET: _pocket_free_internal(header); break;
        case TTAK_ALLOC_TIER_VMA: _vma_free_internal(header); break;
        case TTAK_ALLOC_TIER_BUDDY:
#if EMBEDDED
            ttak_mem_buddy_free(header);
#else
            pthread_mutex_destroy(&header->lock); free(header);
#endif
            break;
        case TTAK_ALLOC_TIER_GENERAL:
            pthread_mutex_destroy(&header->lock);
#ifdef _WIN32
            if (header->is_huge) VirtualFree(header, 0, MEM_RELEASE); else _aligned_free(header);
#else
            if (header->is_huge) munmap(header, actual_total_alloc_size); else free(header);
#endif
            break;
        default:
            pthread_mutex_destroy(&header->lock);
#if EMBEDDED
            ttak_mem_buddy_free(header);
#elif defined(_WIN32)
            if (header->is_huge) VirtualFree(header, 0, MEM_RELEASE); else _aligned_free(header);
#else
            if (header->is_huge) munmap(header, actual_total_alloc_size); else free(header);
#endif
            break;
    }
}

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
                    if (h->tracking_log) snprintf(h->tracking_log, 1024, "{\"event\":\"trace_enabled\",\"ts\":%" PRIu64 "}", ttak_get_tick_count());
                } else if (!enable && h->tracking_log) { free(h->tracking_log); h->tracking_log = NULL; }
                pthread_mutex_unlock(&h->lock);
            }
        }
    }
    pthread_mutex_unlock(&global_map_lock);
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
    for (size_t i = 0; i < count; i++) ttak_mem_free(dirty[i]);
    free(dirty);
}

void TTAK_COLD_PATH **tt_inspect_dirty_pointers(uint64_t now, size_t *count_out) {
    tt_map_t *map_handle = (tt_map_t *)global_ptr_map;
    if (!count_out || !map_handle) return NULL;
    pthread_mutex_lock(&global_map_lock);
    void **dirty = malloc(sizeof(void *) * map_handle->size);
    if (!dirty) { pthread_mutex_unlock(&global_map_lock); return NULL; }
    size_t found = 0;
    for (size_t i = 0; i < map_handle->cap; i++) {
        if (map_handle->tbl[i].ctrl == OCCUPIED) {
            ttak_mem_header_t *h = (ttak_mem_header_t *)map_handle->tbl[i].value;
            if ((h->expires_tick != (uint64_t)-1 && now > h->expires_tick) || ttak_atomic_read64(&h->access_count) > 1000000)
                dirty[found++] = (void*)map_handle->tbl[i].key;
        }
    }
    pthread_mutex_unlock(&global_map_lock); *count_out = found; return dirty;
}

void **tt_autoclean_and_inspect(uint64_t now, size_t *count_out) {
    tt_autoclean_dirty_pointers(now); return tt_inspect_dirty_pointers(now, count_out);
}

_Bool ttak_mem_is_pressure_high(void) { return ttak_atomic_read64(&global_mem_usage) > TTAK_MEM_HIGH_WATERMARK; }

void save_current_progress(const char *filename, const void *data, size_t size) {
    char temp_name[256]; snprintf(temp_name, sizeof(temp_name), "%s.tmp", filename);
    int fd = open(temp_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    if (write(fd, data, size) != (ssize_t)size) { close(fd); unlink(temp_name); return; }
    if (fsync(fd) != 0) { close(fd); unlink(temp_name); return; }
    close(fd); if (rename(temp_name, filename) != 0) unlink(temp_name);
#ifndef _WIN32
    int dfd = open(".", O_RDONLY | O_DIRECTORY); if (dfd >= 0) { fsync(dfd); close(dfd); }
#endif
}

#if EMBEDDED
void ttak_mem_set_embedded_pool(void *pool_start, size_t pool_len) {
    if (!pool_start || pool_len == 0) return;
    pthread_once(&buddy_once, buddy_bootstrap);
    ttak_mem_buddy_set_pool(pool_start, pool_len);
}
#endif
