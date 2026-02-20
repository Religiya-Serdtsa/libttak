/**
 * @file ttak_mem_pocket.c
 * @brief Implementation of the Thread-Local Pocket Allocator.
 *
 * This tier handles small, frequently allocated objects (up to 128 bytes total block)
 * using thread-local freelists to avoid global synchronization.
 */

#ifdef _WIN32
    #include <windows.h>
    static inline void *_ttak_mmap_page(size_t size) {
        return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
#else
    #include <sys/mman.h>
    #include <unistd.h>
    static inline void *_ttak_mmap_page(size_t size) {
        void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return (p == MAP_FAILED) ? NULL : p;
    }
#endif
#include <string.h>
#include <stdio.h>

#include "../../internal/ttak/mem_internal.h"
#include "../../include/ttak/mem/mem.h"

/**
 * @brief Thread-local freelists for each pocket size class.
 */
TTAK_THREAD_LOCAL ttak_mem_pocket_freelist_t ttak_pocket_freelists[TTAK_NUM_POCKET_FREELISTS] = {0};

/**
 * @brief Allocates and populates a new 4KB pocket page.
 * @param freelist_idx The size class index for which the page is being allocated.
 * @return Pointer to the allocated page, or NULL on failure.
 */
static void* allocate_new_pocket_page(int freelist_idx) {
    void* page = _ttak_mmap_page(TTAK_POCKET_PAGE_SIZE);
    if (!page) {
        fprintf(stderr, "ttak_mem_pocket: Failed to allocate new page\n");
        return NULL;
    }
    memset(page, 0, TTAK_POCKET_PAGE_SIZE);

    // Stamp the page-level magic with size class index.
    ((uint32_t*)page)[0] = POCKET_MAGIC | (freelist_idx & 0xFF); 

    size_t total_block_size = get_total_block_size_for_freelist(freelist_idx);
    
    // First block starts at 64-byte offset to maintain alignment.
    char* current_block_ptr = (char*)page + 64; 
    size_t usable_page_space = TTAK_POCKET_PAGE_SIZE - 64;
    size_t num_blocks = usable_page_space / total_block_size;

    // Push blocks onto the thread-local LIFO freelist.
    for (size_t i = 0; i < num_blocks; ++i) {
        void* block = (void*)current_block_ptr;
        *(void**)block = ttak_pocket_freelists[freelist_idx].head;
        ttak_pocket_freelists[freelist_idx].head = block;
        current_block_ptr += total_block_size;
    }
    return page;
}

ttak_mem_header_t* ttak_mem_pocket_alloc_internal(size_t user_requested_size) {
    if (user_requested_size == 0 || user_requested_size > 128) return NULL; 

    t_reentrancy_guard = true;

    size_t total_block_size = sizeof(ttak_mem_header_t) + user_requested_size;
    int idx = get_pocket_size_class_idx(total_block_size);
    if (idx == -1) {
        t_reentrancy_guard = false;
        return NULL;
    }

    ttak_mem_header_t* header_block = (ttak_mem_header_t*)ttak_pocket_freelists[idx].head;
    if (header_block) {
        ttak_pocket_freelists[idx].head = *(void**)header_block;
    } else {
        if (!allocate_new_pocket_page(idx)) {
            t_reentrancy_guard = false;
            return NULL;
        }
        header_block = (ttak_mem_header_t*)ttak_pocket_freelists[idx].head;
        if (header_block) {
             ttak_pocket_freelists[idx].head = *(void**)header_block;
        } else {
            t_reentrancy_guard = false;
            return NULL;
        }
    }
    t_reentrancy_guard = false;
    return header_block;
}

void _pocket_free_internal(ttak_mem_header_t* header) {
    if (!header) return;

    t_reentrancy_guard = true;

    // Determine the page start to extract the size class index.
    uintptr_t page_start_addr = (uintptr_t)header & ~((uintptr_t)TTAK_POCKET_PAGE_SIZE - 1);
    uint32_t page_magic_val = ((uint32_t*)page_start_addr)[0];

    if ((page_magic_val & 0xFFFFFF00) != POCKET_MAGIC) {
        fprintf(stderr, "ttak_mem_pocket: Freeing non-pocket allocated header %p\n", (void*)header);
        t_reentrancy_guard = false;
        return;
    }

    int idx = page_magic_val & 0xFF;
    if (idx < 0 || idx >= TTAK_NUM_POCKET_FREELISTS) {
        fprintf(stderr, "ttak_mem_pocket: Corrupted freelist index for header %p\n", (void*)header);
        t_reentrancy_guard = false;
        return;
    }

    // Push back to the LIFO freelist.
    *(void**)header = ttak_pocket_freelists[idx].head;
    ttak_pocket_freelists[idx].head = header;

    t_reentrancy_guard = false;
}
