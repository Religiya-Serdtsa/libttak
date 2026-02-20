/**
 * @file mem_internal.h
 * @brief Internal memory subsystem definitions and tiered allocator interfaces.
 *
 * This file contains declarations for internal allocator modules (Pocket, VMA, Slab)
 * and shared reentrancy/friction guards used across the TTAK memory implementation.
 */

#ifndef TTAK_MEM_INTERNAL_H
#define TTAK_MEM_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include "../../internal/app_types.h"
#include <ttak/mem/mem.h>

#ifndef TTAK_UNLIKELY
#define TTAK_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

// --- Memory Magic Numbers ---
#define POCKET_MAGIC 0x80C4E700 /**< Base magic for 4KB pocket pages (Lower bits: freelist_idx) */
#define SLAB_MAGIC   0x51ABCA5E /**< Magic for Slab pages */

/**
 * @brief Reentrancy guard to prevent recursive calls during boot/allocation.
 * Defined in mem.c.
 */
extern TTAK_THREAD_LOCAL bool t_reentrancy_guard;

/**
 * @typedef ttak_fixed_16_16_t
 * @brief 16.16 Fixed-point type for friction calculation.
 */
typedef int32_t ttak_fixed_16_16_t;

#define TTAK_FP_ONE (1 << 16)
#define TTAK_FP_FROM_INT(val) ((ttak_fixed_16_16_t)(val) << 16)
#define TTAK_FP_TO_INT(val) ((val) >> 16)
#define TTAK_FP_MUL(a, b) (((int64_t)(a) * (b)) >> 16)
#define TTAK_FP_DIV(a, b) (((int64_t)(a) << 16) / (b))

/**
 * @struct ttak_mem_friction_matrix_t
 * @brief Matrix for tracking memory pressure and "Mechanical Damping".
 */
typedef struct {
    _Atomic ttak_fixed_16_16_t values[4];   /**< Per-size-class friction values */
    _Atomic ttak_fixed_16_16_t global_friction; /**< Resulting product friction */
    ttak_fixed_16_16_t pressure_threshold;      /**< High friction rejection point */
} ttak_mem_friction_matrix_t;

/**
 * @brief Global friction matrix instance.
 * Defined in mem.c.
 */
extern ttak_mem_friction_matrix_t global_friction_matrix;

// Forward declare ttak_mem_header_t
typedef struct ttak_mem_header_t ttak_mem_header_t;

// --- Thread-Local Pockets (Objects <= 128B total block) ---
#define TTAK_POCKET_PAGE_SIZE 4096 
#define TTAK_POCKET_ALIGNMENT 4096 
#define TTAK_NUM_POCKET_FREELISTS 2 

/**
 * @struct ttak_mem_pocket_freelist
 * @brief Simple LIFO freelist for thread-local blocks.
 */
typedef struct ttak_mem_pocket_freelist {
    void* head; /**< Top of the free-stack */
} ttak_mem_pocket_freelist_t;

/**
 * @brief Thread-local pocket freelists.
 * Defined in ttak_mem_pocket.c.
 */
extern TTAK_THREAD_LOCAL ttak_mem_pocket_freelist_t ttak_pocket_freelists[TTAK_NUM_POCKET_FREELISTS];

// --- Bare-Metal VMA (Virtual Mapping Area) ---
#define TTAK_VMA_REGION_SIZE (16 * 1024 * 1024) 
#define TTAK_VMA_ALIGNMENT 64 

/**
 * @struct ttak_mem_vma_region
 * @brief Linear virtual mapping area for lock-free bump allocation.
 */
typedef struct ttak_mem_vma_region {
    void* start_addr;               /**< Base address of the mmap'd region */
    _Atomic uintptr_t current_cursor; /**< Atomic cursor for linear allocation */
} ttak_mem_vma_region_t;

/**
 * @brief Global VMA region instance.
 * Defined in ttak_mem_vma.c.
 */
extern ttak_mem_vma_region_t global_vma_region;

/**
 * @struct ttak_slab_t
 * @brief Metadata for Slab vanguard (64KB - 512KB Dynamic Path).
 */
typedef struct {
    uint32_t magic;         /**< SLAB_MAGIC */
    uint32_t block_size;    /**< Size of objects in this slab */
    uint32_t total_blocks;
    _Atomic uint32_t active_count;
    void* free_stack;
    void* page_start;
} ttak_slab_t;

// --- Internal Tiered Allocator Interfaces ---

/**
 * @brief Allocates from the thread-local pocket tier.
 * @param user_requested_size Size requested by user.
 * @return Allocated header, or NULL.
 */
ttak_mem_header_t* ttak_mem_pocket_alloc_internal(size_t user_requested_size);

/**
 * @brief Frees a header back to the thread-local pocket.
 * @param header Header to return.
 */
void _pocket_free_internal(ttak_mem_header_t* header);

/**
 * @brief Allocates from the lock-free VMA tier.
 * @param user_requested_size Size requested by user.
 * @return Allocated header, or NULL.
 */
ttak_mem_header_t* ttak_mem_vma_alloc_internal(size_t user_requested_size);

/**
 * @brief Release VMA allocation (currently no-op for linear allocator).
 * @param header Header to release.
 */
void _vma_free_internal(ttak_mem_header_t* header);

/**
 * @brief Raw linear VMA allocator (internal use).
 * @param size Total bytes to allocate.
 * @return Start pointer.
 */
void* _vma_alloc_linear(size_t size);

/**
 * @brief Slab rectified allocator (Dynamic path).
 */
ttak_mem_header_t* _slab_alloc_rectified(size_t user_requested_size);

/**
 * @brief Returns slab block to pool.
 */
void _slab_free_internal(ttak_mem_header_t* header, uintptr_t slab_page_start_addr);

/**
 * @brief Maps a total block size to a pocket freelist index.
 */
static inline int get_pocket_size_class_idx(size_t total_block_size) {
    if (total_block_size <= 192) return 0;
    if (total_block_size <= 256) return 1;
    return -1;
}

/**
 * @brief Returns the total block size for a given pocket index.
 */
static inline size_t get_total_block_size_for_freelist(int idx) {
    switch (idx) {
        case 0: return 192;
        case 1: return 256;
    }
    return 0;
}

#endif // TTAK_MEM_INTERNAL_H
