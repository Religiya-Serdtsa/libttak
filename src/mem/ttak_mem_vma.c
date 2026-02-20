/**
 * @file ttak_mem_vma.c
 * @brief Implementation of the Bare-Metal VMA (Virtual Mapping Area) Tier.
 *
 * This tier provides a lock-free bump allocator for medium-sized objects,
 * utilizing a large pre-mapped virtual address space for rapid allocation.
 */

#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "../../internal/ttak/mem_internal.h"
#include "../../include/ttak/mem/mem.h"

/**
 * @brief Global VMA region instance.
 */
ttak_mem_vma_region_t global_vma_region = {
    .start_addr = NULL,
    .current_cursor = (uintptr_t)NULL
};

/**
 * @brief Guard for one-time initialization of the VMA region.
 */
static pthread_once_t vma_init_once = PTHREAD_ONCE_INIT;

/**
 * @brief Helper function to initialize the VMA region via mmap.
 */
static void _init_vma_region(void) {
    void* addr = mmap(NULL, TTAK_VMA_REGION_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        fprintf(stderr, "ttak_mem_vma: Failed to mmap VMA region: %s\n", strerror(errno));
        return;
    }

    global_vma_region.start_addr = addr;
    atomic_store(&global_vma_region.current_cursor, (uintptr_t)addr);
    fprintf(stderr, "ttak_mem_vma: VMA region initialized at %p, size %uMB\n", 
            global_vma_region.start_addr, (unsigned int)(TTAK_VMA_REGION_SIZE / (1024 * 1024)));
}

ttak_mem_header_t* ttak_mem_vma_alloc_internal(size_t user_requested_size) {
    if (user_requested_size == 0) return NULL;

    pthread_once(&vma_init_once, _init_vma_region);
    
    if (global_vma_region.start_addr == NULL) {
        return NULL;
    }

    t_reentrancy_guard = true;

    size_t total_alloc_size = sizeof(ttak_mem_header_t) + user_requested_size;
    // Align block to TTAK_VMA_ALIGNMENT (64-byte).
    size_t aligned_total_alloc_size = (total_alloc_size + TTAK_VMA_ALIGNMENT - 1) & ~((size_t)TTAK_VMA_ALIGNMENT - 1);
    
    uintptr_t old_cursor;
    uintptr_t new_cursor;

    do {
        old_cursor = atomic_load(&global_vma_region.current_cursor);
        uintptr_t current_aligned_start = (old_cursor + TTAK_VMA_ALIGNMENT - 1) & ~((uintptr_t)TTAK_VMA_ALIGNMENT - 1);
        new_cursor = current_aligned_start + aligned_total_alloc_size;

        if (new_cursor > (uintptr_t)global_vma_region.start_addr + TTAK_VMA_REGION_SIZE) {
            fprintf(stderr, "ttak_mem_vma: VMA region exhausted for size %zu\n", aligned_total_alloc_size);
            t_reentrancy_guard = false;
            return NULL; 
        }

        // Lock-free cursor advancement.
    } while (!atomic_compare_exchange_weak(&global_vma_region.current_cursor, &old_cursor, new_cursor));

    ttak_mem_header_t* allocated_header = (ttak_mem_header_t*)((old_cursor + TTAK_VMA_ALIGNMENT - 1) & ~((uintptr_t)TTAK_VMA_ALIGNMENT - 1));
    memset(allocated_header, 0, aligned_total_alloc_size);

    t_reentrancy_guard = false;
    return allocated_header;
}

void _vma_free_internal(ttak_mem_header_t* header) {
    // Bump allocator: individual frees are no-ops.
    (void)header;
    t_reentrancy_guard = false;
}

/**
 * @brief Unmaps the entire VMA region on process exit.
 */
__attribute__((destructor))
static void _destroy_vma_region(void) {
    if (global_vma_region.start_addr) {
        munmap(global_vma_region.start_addr, TTAK_VMA_REGION_SIZE);
        global_vma_region.start_addr = NULL;
        atomic_store(&global_vma_region.current_cursor, (uintptr_t)NULL);
        fprintf(stderr, "ttak_mem_vma: VMA region unmapped.\n");
    }
}
