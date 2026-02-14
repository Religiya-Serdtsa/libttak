#ifndef TTAK_MEM_H
#define TTAK_MEM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <pthread.h>
#include <ttak/atomic/atomic.h>

/**
 * @brief Alignment for cache-line optimization (64-byte).
 */
#define TTAK_CACHE_LINE_SIZE 64

/**
 * @brief Macro to indicate that the allocated memory should persist forever.
 */
#define __TTAK_UNSAFE_MEM_FOREVER__ ((uint64_t)-1)

/**
 * @brief "Fortress" Magic Number for header validation.
 */
#define TTAK_MAGIC_NUMBER 0x5454414B

/**
 * @brief Sentinel for invalidated references.
 */
#define SAFE_NULL NULL

/**
 * @brief "Fortress" Memory Header.
 * 64-byte aligned to prevent False Sharing and ensure user pointer alignment.
 * Total size is 128 bytes to maintain 64-byte alignment for user data.
 */
typedef struct {
    alignas(64) uint32_t magic;         /**< 0x5454414B */
    uint32_t checksum;      /**< Metadata checksum */
    uint64_t created_tick;  /**< Creation timestamp */
    uint64_t expires_tick;  /**< Expiration timestamp */
    uint64_t access_count;  /**< Atomic access audit counter */
    uint64_t pin_count;     /**< Atomic reference count for pinning */
    size_t   size;          /**< User-requested size */
    pthread_mutex_t lock;   /**< Per-header synchronization */
    _Bool    freed;         /**< Allocation status */
    _Bool    is_const;      /**< Immutability hint */
    _Bool    is_volatile;   /**< Volatility hint */
    _Bool    allow_direct_access; /**< Safety bypass flag */
    _Bool    is_huge;       /**< Mapped via hugepages */
    _Bool    should_join;   /**< Indicates if associated resource needs joining */
    _Bool    strict_check; _Bool    is_root;  /**< Enable strict memory boundary checks */
    uint64_t canary_start;  /**< Magic number for start of user data */
    uint64_t canary_end;    /**< Magic number for end of user data */
    char     *tracking_log;  /**< Memory operation tracking log (dynamic) */
    char     reserved[11];  /**< Explicit padding for header alignment */
} ttak_mem_header_t;

/**
 * @brief Memory allocation flags.
 */
typedef enum {
    TTAK_MEM_DEFAULT = 0,
    TTAK_MEM_HUGE_PAGES = (1 << 0), /** Try to use 2MB/1GB pages */
    TTAK_MEM_CACHE_ALIGNED = (1 << 1), /** Force 64-byte alignment */
    TTAK_MEM_STRICT_CHECK = (1 << 2) /** Enable strict memory boundary checks */
} ttak_mem_flags_t;

/**
 * @brief Unified memory allocation with lifecycle management and hardware optimization.
 */
void *ttak_mem_alloc_safe(size_t size, uint64_t lifetime_ticks, uint64_t now, _Bool is_const, _Bool is_volatile, _Bool allow_direct, _Bool is_root, ttak_mem_flags_t flags);

/**
 * @brief Reallocates memory with lifecycle management.
 */
void *ttak_mem_realloc_safe(void *ptr, size_t new_size, uint64_t lifetime_ticks, uint64_t now, _Bool is_root, ttak_mem_flags_t flags);

/**
 * @brief Duplicates a memory block with lifecycle management.
 * Provides a highly optimized internal path for cloning structures and buffers.
 */
void *ttak_mem_dup_safe(const void *src, size_t size, uint64_t lifetime_ticks, uint64_t now, _Bool is_root, ttak_mem_flags_t flags);

/**
 * @brief Frees the memory block and removes it from the global shadow map.
 */
void ttak_mem_free(void *ptr);

/**
 * @brief Accesses the memory block, verifying its lifecycle and security.
 * Inlined for maximum performance ("grotesque tweak").
 */
static inline void *ttak_mem_access(void *ptr, uint64_t now) {
    if (!ptr) return NULL;
    ttak_mem_header_t *header = (ttak_mem_header_t *)ptr - 1;

    // Fast path: Optimistic lock-free check
    if (header->magic != TTAK_MAGIC_NUMBER) return NULL; // Basic sanity
    if (header->freed) return NULL;
    if (header->expires_tick != __TTAK_UNSAFE_MEM_FOREVER__ && now > header->expires_tick) return NULL;
    if (!header->allow_direct_access) return NULL;

    // Atomic increment without lock
    // Using GCC/Clang builtin for speed if available, else ttak wrapper
    // ttak_atomic_inc64 is likely a wrapper.
    // Let's use the library's function. 
    // Since it's inline, we need to ensure ttak_atomic_inc64 is visible. It is (included above).
    ttak_atomic_inc64(&header->access_count);

    return ptr;
}

/**
 * @brief Inspects and returns pointers that are expired or have abnormal access counts.
 */
void **tt_inspect_dirty_pointers(uint64_t now, size_t *count_out);

/**
 * @brief Automatically cleans up expired memory blocks with adaptive scheduling.
 */
void tt_autoclean_dirty_pointers(uint64_t now);

/**
 * @brief Configures the global background GC (mem_tree) parameters.
 */
void ttak_mem_configure_gc(uint64_t min_interval_ns, uint64_t max_interval_ns, size_t pressure_threshold);

/**
 * @brief Cleans up and returns abnormal pointers.
 */
void **tt_autoclean_and_inspect(uint64_t now, size_t *count_out);

/**
 * @brief Sets the global memory tracing flag.
 */
void ttak_mem_set_trace(int enable);

/**
 * @brief Returns whether memory tracing is currently enabled.
 */
int ttak_mem_is_trace_enabled(void);

/**
 * @brief Calculates a 32-bit checksum for the memory header.
 */
static inline uint32_t ttak_calc_header_checksum(const ttak_mem_header_t *h) {
    uint32_t sum1 = h->magic;
    uint32_t sum2 = (uint32_t)h->created_tick;
    sum1 ^= (uint32_t)(h->created_tick >> 32);
    sum2 ^= (uint32_t)h->expires_tick;
    sum1 ^= (uint32_t)(h->expires_tick >> 32);
    sum2 ^= (uint32_t)h->size;
#if defined(__LP64__) || defined(_WIN64)
    sum1 ^= (uint32_t)(h->size >> 32);
#endif
    sum2 ^= (uint32_t)h->should_join;
    sum1 ^= (uint32_t)h->strict_check;
    sum2 ^= (uint32_t)h->is_root;
    sum1 ^= (uint32_t)h->canary_start;
    sum2 ^= (uint32_t)(h->canary_start >> 32);
    sum1 ^= (uint32_t)h->canary_end;
    sum2 ^= (uint32_t)(h->canary_end >> 32);
    return sum1 ^ sum2;
}

/* Compatibility macros */
typedef void ttak_lifecycle_obj_t;
#define ttak_mem_alloc(size, lifetime, now) ttak_mem_alloc_safe(size, lifetime, now, false, false, true, false, TTAK_MEM_DEFAULT)
#define ttak_mem_alloc_with_flags(size, lifetime, now, flags) ttak_mem_alloc_safe(size, lifetime, now, false, false, true, false, flags)
#define ttak_mem_realloc(ptr, size, lifetime, now) ttak_mem_realloc_safe(ptr, size, lifetime, now, false, TTAK_MEM_DEFAULT)
#define ttak_mem_realloc_with_flags(ptr, size, lifetime, now, flags) ttak_mem_realloc_safe(ptr, size, lifetime, now, false, flags)
#define ttak_mem_dup(src, size, lifetime, now) ttak_mem_dup_safe(src, size, lifetime, now, false, TTAK_MEM_DEFAULT)
#define ttak_mem_dup_with_flags(src, size, lifetime, now, flags) ttak_mem_dup_safe(src, size, lifetime, now, false, flags)

#endif // TTAK_MEM_H
