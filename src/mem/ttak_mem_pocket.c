/**
 * @file ttak_mem_pocket.c
 * @brief Implementation of the Thread-Local Pocket Allocator.
 *
 * This tier handles small, frequently allocated objects (up to 128 bytes total block)
 * using thread-local freelists to avoid global synchronization.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if defined(__TINYC__)
#include <pthread.h>
#endif

#include "../../internal/ttak/mem_internal.h"
#include "../../include/ttak/mem/mem.h"
#include <ttak/mem/fastpath.h>

/**
 * @brief Thread-local freelists for each pocket size class.
 */
#if defined(__TINYC__)
static pthread_once_t pocket_tls_once = PTHREAD_ONCE_INIT;
static pthread_key_t pocket_tls_key;

static void pocket_tls_init(void) {
    pthread_key_create(&pocket_tls_key, free);
}

ttak_mem_pocket_freelist_t *ttak_tls_get_pocket_freelists(void) {
    pthread_once(&pocket_tls_once, pocket_tls_init);
    ttak_mem_pocket_freelist_t *lists = pthread_getspecific(pocket_tls_key);
    if (!lists) {
        lists = calloc(TTAK_NUM_POCKET_FREELISTS, sizeof(*lists));
        if (lists) {
            if (pthread_setspecific(pocket_tls_key, lists) != 0) {
                free(lists);
                lists = NULL;
            }
        }
    }
    if (!lists) {
        static ttak_mem_pocket_freelist_t fallback[TTAK_NUM_POCKET_FREELISTS];
        return fallback;
    }
    return lists;
}
#else
TTAK_THREAD_LOCAL ttak_mem_pocket_freelist_t ttak_pocket_freelists[TTAK_NUM_POCKET_FREELISTS] = {0};
#endif

typedef struct ttak_pocket_page_meta_t {
    uint32_t magic_with_idx;
    uintptr_t owner_thread;
} ttak_pocket_page_meta_t;

static pthread_mutex_t pocket_page_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pocket_remote_lock = PTHREAD_MUTEX_INITIALIZER;
static void *pocket_remote_freelists[TTAK_NUM_POCKET_FREELISTS] = {0};
static _Alignas(TTAK_POCKET_ALIGNMENT) uint8_t pocket_page_pool[TTAK_POCKET_PAGE_SIZE * 2048];
static size_t pocket_page_pool_cursor = 0;

static uintptr_t ttak_thread_identity(void) {
    return (uintptr_t)pthread_self();
}

static void* allocate_new_pocket_page(int freelist_idx) {
    pthread_mutex_lock(&pocket_page_pool_lock);
    if (pocket_page_pool_cursor + TTAK_POCKET_PAGE_SIZE > sizeof(pocket_page_pool)) {
        pthread_mutex_unlock(&pocket_page_pool_lock);
        fprintf(stderr, "ttak_mem_pocket: Failed to allocate new page\n");
        return NULL;
    }
    void *page = (void *)(pocket_page_pool + pocket_page_pool_cursor);
    pocket_page_pool_cursor += TTAK_POCKET_PAGE_SIZE;
    pthread_mutex_unlock(&pocket_page_pool_lock);

    ttak_mem_stream_zero(page, TTAK_POCKET_PAGE_SIZE);

    ttak_pocket_page_meta_t *page_meta = (ttak_pocket_page_meta_t *)page;
    page_meta->magic_with_idx = POCKET_MAGIC | (freelist_idx & 0xFF);
    page_meta->owner_thread = ttak_thread_identity();

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
    if (user_requested_size == 0 || user_requested_size > 512) return NULL;

    // Determined size class index.
    size_t total_block_size = sizeof(ttak_mem_header_t) + user_requested_size;
    int idx = get_pocket_size_class_idx(total_block_size);
    if (idx == -1) {
        return NULL;
    }

    ttak_mem_header_t* header_block = (ttak_mem_header_t*)ttak_pocket_freelists[idx].head;
    if (header_block) {
        ttak_pocket_freelists[idx].head = *(void**)header_block;
    } else {
        pthread_mutex_lock(&pocket_remote_lock);
        if (pocket_remote_freelists[idx]) {
            header_block = (ttak_mem_header_t*)pocket_remote_freelists[idx];
            pocket_remote_freelists[idx] = *(void **)header_block;
            pthread_mutex_unlock(&pocket_remote_lock);
            return header_block;
        }
        pthread_mutex_unlock(&pocket_remote_lock);
        if (!allocate_new_pocket_page(idx)) {
            return NULL;
        }
        header_block = (ttak_mem_header_t*)ttak_pocket_freelists[idx].head;
        if (header_block) {
             ttak_pocket_freelists[idx].head = *(void**)header_block;
        } else {
            return NULL;
        }
    }
    return header_block;
}

void _pocket_free_internal(ttak_mem_header_t* header) {
    if (!header) return;

    // Determine the page start to extract the size class index.
    uintptr_t page_start_addr = (uintptr_t)header & ~((uintptr_t)TTAK_POCKET_PAGE_SIZE - 1);
    ttak_pocket_page_meta_t *page_meta = (ttak_pocket_page_meta_t *)page_start_addr;
    uint32_t page_magic_val = page_meta->magic_with_idx;

    if ((page_magic_val & 0xFFFFFF00) != POCKET_MAGIC) {
        fprintf(stderr, "ttak_mem_pocket: Freeing non-pocket allocated header %p\n", (void*)header);
        return;
    }

    int idx = page_magic_val & 0xFF;
    if (idx < 0 || idx >= TTAK_NUM_POCKET_FREELISTS) {
        fprintf(stderr, "ttak_mem_pocket: Corrupted freelist index for header %p\n", (void*)header);
        return;
    }

    if (page_meta->owner_thread == ttak_thread_identity()) {
        *(void**)header = ttak_pocket_freelists[idx].head;
        ttak_pocket_freelists[idx].head = header;
        return;
    }

    pthread_mutex_lock(&pocket_remote_lock);
    *(void**)header = pocket_remote_freelists[idx];
    pocket_remote_freelists[idx] = header;
    pthread_mutex_unlock(&pocket_remote_lock);
}
