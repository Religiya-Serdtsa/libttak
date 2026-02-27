/**
 * @file mem.h
 * @brief TTAK Unified Memory Subsystem with Lifecycle Management and Hardware Optimization.
 *
 * This header defines the "Fortress" memory allocation system, which provides:
 * - Tiered allocation (Thread-Local Pockets, Bare-Metal VMA, System Allocator)
 * - Automatic lifecycle management with tick-based expiration
 * - Security features (Magic numbers, Checksums, Canaries)
 * - Hardware optimizations (Cache-line alignment, Huge pages)
 */

#ifndef TTAK_MEM_H
#define TTAK_MEM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <ttak/types/ttak_compiler.h>
#include <stdalign.h>
#include <pthread.h>
#ifndef _MSC_VER
#include <stdatomic.h>
#include <ttak/mem/epoch_gc.h>
#define TTAK_ATOMIC_FETCH_ADD_U64(ptr, val) atomic_fetch_add((_Atomic uint64_t *)(ptr), (val))
#else
#include <windows.h>
#include <stdbool.h>
typedef bool _Bool;
#define TTAK_ATOMIC_FETCH_ADD_U64(ptr, val) InterlockedExchangeAdd64((volatile LONG64 *)(ptr), (LONG64)(val))
#endif

/**
 * @enum ttak_allocation_tier_t
 * @brief Defines the memory tier used for an allocation.
 */
typedef enum {
    TTAK_ALLOC_TIER_UNKNOWN = 0,    /**< Tier unknown or corrupted */
    TTAK_ALLOC_TIER_POCKET,         /**< Allocated from a Thread-Local Pocket (Small objects) */
    TTAK_ALLOC_TIER_VMA,            /**< Allocated from Bare-Metal VMA (Medium objects) */
    TTAK_ALLOC_TIER_SLAB,           /**< Allocated from a Slab allocator (Reserved) */
    TTAK_ALLOC_TIER_BUDDY,          /**< Allocated from the Buddy System (Embedded Mode) */
    TTAK_ALLOC_TIER_GENERAL,        /**< Allocated via general system allocator (Large objects) */
} ttak_allocation_tier_t;

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
 * @struct ttak_mem_header_t
 * @brief "Fortress" Memory Header stored before user data.
 *
 * 64-byte aligned to prevent False Sharing and ensure user pointer alignment.
 * The structure is padded to maintain alignment for the following user data.
 */
typedef struct ttak_mem_header_t {
    alignas(64) uint32_t magic;         /**< 0x5454414B */
    uint32_t checksum;                  /**< Metadata checksum to detect header corruption */
    uint64_t created_tick;              /**< Creation timestamp in ticks */
    uint64_t expires_tick;              /**< Expiration timestamp in ticks */
    uint64_t access_count;              /**< Atomic access audit counter */
    uint64_t pin_count;                 /**< Atomic reference count for pinning */
    size_t   size;                      /**< User-requested size in bytes */
    pthread_mutex_t lock;               /**< Per-header synchronization lock */
    uint8_t  freed;                     /**< True if the block has been deallocated */
    uint8_t  is_const;                  /**< Immutability hint */
    uint8_t  is_volatile;               /**< Volatility hint */
    uint8_t  allow_direct_access;       /**< Safety bypass flag for direct pointer access */
    uint8_t  is_huge;                   /**< True if mapped via hugepages */
    uint8_t  should_join;               /**< Indicates if associated resource needs joining */
    uint8_t  strict_check;              /**< Enable strict memory boundary (canary) checks */
    uint8_t  is_root;                   /**< Marks the allocation as a root node for the mem_tree */
    uint64_t canary_start;              /**< Magic number for start of user data (in strict mode) */
    uint64_t canary_end;                /**< Magic number for end of user data (in strict mode) */
    char     *tracking_log;             /**< Dynamic memory operation tracking log (JSON) */
    uint8_t  allocation_tier;           /**< Tier that performed the allocation */
    char     reserved[10];              /**< Explicit padding for header alignment */
} ttak_mem_header_t;

/**
 * @enum ttak_mem_flags_t
 * @brief Memory allocation behavior flags.
 */
typedef enum {
    TTAK_MEM_DEFAULT = 0,               /**< Default allocation behavior */
    TTAK_MEM_HUGE_PAGES = (1 << 0),     /**< Try to use 2MB/1GB pages */
    TTAK_MEM_CACHE_ALIGNED = (1 << 1),  /**< Force 64-byte cache alignment */
    TTAK_MEM_STRICT_CHECK = (1 << 2),   /**< Enable strict boundary/canary checks */
    TTAK_MEM_LOW_PRIORITY = (1 << 3)    /**< Reject if under memory pressure/high friction */
} ttak_mem_flags_t;

/**
 * @brief Unified memory allocation with lifecycle management.
 * @param size Number of bytes requested.
 * @param lifetime_ticks Lifetime hint in ticks (__TTAK_UNSAFE_MEM_FOREVER__ for infinite).
 * @param now_tick Current timestamp in ticks.
 * @param is_const Marks the buffer as immutable.
 * @param is_volatile Indicates volatile access patterns.
 * @param allow_direct If false, direct access via ttak_mem_access is restricted.
 * @param is_root Marks the allocation as a root for garbage collection.
 * @param flags Allocation behavior flags.
 * @return Pointer to zeroed user memory, or NULL on failure.
 */
void *ttak_mem_alloc_safe(size_t size, uint64_t lifetime_ticks, uint64_t now_tick, bool is_const, bool is_volatile, bool allow_direct, bool is_root, ttak_mem_flags_t flags);
/* @brief Wrapper for easy allocation with auto GC registering */
void *ttak_fastalloc(ttak_epoch_gc_t *gc, size_t size, uint64_t lifetime_ticks, uint64_t now_tick);
/**
 * @brief Reallocates memory with lifecycle management.
 * @param ptr Existing allocation pointer.
 * @param new_size Requested new size.
 * @param lifetime_ticks Updated lifetime hint.
 * @param now_tick Current timestamp in ticks.
 * @param is_root Whether the new allocation is a root node.
 * @param flags Allocation behavior flags.
 * @return Reallocated pointer, or NULL on failure.
 */
void *ttak_mem_realloc_safe(void *ptr, size_t new_size, uint64_t lifetime_ticks, uint64_t now_tick, bool is_root, ttak_mem_flags_t flags);
/**
 * @brief Primitive allocator for internal subsystem bootstrap.
 *
 * Bypasses all managed logic (Header, Map, Tree) to prevent recursive cycles
 * during early initialization of the epoch or memory subsystems.
 *
 * @param size Number of bytes requested.
 * @return Pointer to zeroed user memory (64-byte aligned), or NULL on failure.
 */

void *ttak_dangerous_alloc(size_t size);

/**
 * @brief Primitive calloc for internal subsystem bootstrap.
 *
 * @param nmemb Number of elements.
 * @param size Size of each element.
 * @return Pointer to zeroed user memory (64-byte aligned), or NULL on failure.
 */
void *ttak_dangerous_calloc(size_t nmemb, size_t size);

/**
 * @brief Primitive deallocator for internal subsystem cleanup.
 *
 * Directly returns memory to the underlying system allocator or buddy pool
 * without performing any managed header or checksum validations.
 *
 * @param ptr Pointer to memory allocated via ttak_dangerous_alloc.
 */
void ttak_dangerous_free(void *ptr);

/**
 * @brief Frees a memory block and updates usage statistics.
 * @param ptr Pointer to user memory.
 */
void ttak_mem_free(void *ptr);

/**
 * @brief Duplicates a memory block with lifecycle management.
 * @param src Source memory block.
 * @param size Number of bytes to copy.
 * @param lifetime_ticks Updated lifetime hint.
 * @param now_tick Current timestamp in ticks.
 * @param is_root Whether the new allocation is a root node.
 * @param flags Allocation behavior flags.
 * @return Duplicated pointer, or NULL on failure.
 */
void *ttak_mem_dup_safe(const void *src, size_t size, uint64_t lifetime_ticks, uint64_t now_tick, bool is_root, ttak_mem_flags_t flags);

/**
 * @brief Frees a memory block and updates usage statistics.
 * @param ptr Pointer to user memory.
 */
void ttak_mem_free(void *ptr);

/**
 * @brief Accesses a memory block, verifying its lifecycle and security.
 * @param ptr Pointer to user memory.
 * @param now_tick Current timestamp in ticks.
 * @return Validated pointer, or NULL if security check fails or block is expired.
 */
static inline void *ttak_mem_access(void *ptr, uint64_t now_tick) {
    if (!ptr) return NULL;
    ttak_mem_header_t *header = (ttak_mem_header_t *)ptr - 1;

    if (header->magic != TTAK_MAGIC_NUMBER) return NULL;
    if (header->freed) return NULL;
    if (header->expires_tick != __TTAK_UNSAFE_MEM_FOREVER__ && now_tick > header->expires_tick) return NULL;
    if (!header->allow_direct_access) return NULL;

    TTAK_ATOMIC_FETCH_ADD_U64(&header->access_count, 1ULL);
    return ptr;
}

/**
 * @brief Inspects for "dirty" pointers (expired or over-accessed).
 * @param now_tick Current timestamp.
 * @param count_out Pointer to store the number of dirty pointers found.
 * @return Array of pointers (caller must free), or NULL.
 */
void **tt_inspect_dirty_pointers(uint64_t now_tick, size_t *count_out);

/**
 * @brief Automatically cleans up expired memory blocks.
 * @param now_tick Current timestamp.
 */
void tt_autoclean_dirty_pointers(uint64_t now_tick);

/**
 * @brief Configures background GC parameters.
 * @param min_interval_ns Minimum sweep interval.
 * @param max_interval_ns Maximum sweep interval.
 * @param pressure_threshold Memory pressure threshold to trigger damping.
 */
void ttak_mem_configure_gc(uint64_t min_interval_ns, uint64_t max_interval_ns, size_t pressure_threshold);

/**
 * @brief Sweeps and returns dirty pointers in a single pass.
 * @param now_tick Current timestamp.
 * @param count_out Pointer to store found count.
 * @return Array of dirty pointers.
 */
void **tt_autoclean_and_inspect(uint64_t now_tick, size_t *count_out);

/**
 * @brief Sets the global memory tracing flag.
 * @param enable Non-zero to enable JSON tracing to stderr.
 */
void ttak_mem_set_trace(int enable);

/**
 * @brief Checks if memory tracing is enabled.
 * @return Non-zero if enabled.
 */
int ttak_mem_is_trace_enabled(void);

/**
 * @brief Calculates a 32-bit checksum for the memory header.
 * @param h Pointer to the header.
 * @return Calculated checksum.
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
    sum1 ^= (uint32_t)h->allocation_tier;
    return sum1 ^ sum2;
}

/* Compatibility macros */
typedef void ttak_lifecycle_obj_t;
#define ttak_mem_alloc(size, lifetime, now_tick) ttak_mem_alloc_safe(size, lifetime, now_tick, false, false, true, false, TTAK_MEM_DEFAULT)
#define ttak_mem_alloc_with_flags(size, lifetime, now_tick, flags) ttak_mem_alloc_safe(size, lifetime, now_tick, false, false, true, false, flags)
#define ttak_mem_realloc(ptr, size, lifetime, now_tick) ttak_mem_realloc_safe(ptr, size, lifetime, now_tick, false, TTAK_MEM_DEFAULT)
#define ttak_mem_realloc_with_flags(ptr, size, lifetime, now_tick, flags) ttak_mem_realloc_safe(ptr, size, lifetime, now_tick, false, flags)
#define ttak_mem_dup(src, size, lifetime, now_tick) ttak_mem_dup_safe(src, size, lifetime, now_tick, false, TTAK_MEM_DEFAULT)
#define ttak_mem_dup_with_flags(src, size, lifetime, now_tick, flags) ttak_mem_dup_safe(src, size, lifetime, now_tick, false, flags)

#ifndef EMBEDDED
#define EMBEDDED 0
#endif

#if EMBEDDED
void ttak_mem_buddy_init(void *pool_start, size_t pool_len, int embedded_mode);
void ttak_mem_buddy_set_pool(void *pool_start, size_t pool_len);
void ttak_mem_set_embedded_pool(void *pool_start, size_t pool_len);
#endif

#endif // TTAK_MEM_H
