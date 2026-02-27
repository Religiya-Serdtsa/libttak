/**
 * @file ttak_shared.c
 * @brief Implementation of shared memory resource management with ttak_dynamic_mask_t.
 * @author Gemini
 * @date 2026-02-08
 */

#include <ttak/shared/shared.h>
#include <ttak/timing/timing.h>
#include <ttak/mem/mem.h>
#include <ttak/mem/epoch.h>
#include <ttak/types/ttak_compiler.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdatomic.h>

/* Lock-free helper to access thread's designated shard slot */
static inline atomic_uint_least64_t* _ttak_shared_get_shard(ttak_shared_t *self) {
    if (TTAK_UNLIKELY(!t_local_state)) ttak_epoch_register_thread();
    uint32_t tid = t_local_state->logical_tid;
    
    uint32_t page_idx = tid >> TTAK_SHARD_PAGE_SHIFT;
    uint32_t slot_idx = tid & TTAK_SHARD_PAGE_MASK;
    
    if (TTAK_UNLIKELY(page_idx >= TTAK_SHARD_DIR_SIZE)) return NULL;
    
    atomic_uint_least64_t *page = atomic_load_explicit(&self->shards.dir[page_idx], memory_order_acquire);
    if (TTAK_UNLIKELY(!page)) {
        atomic_uint_least64_t *new_page = (atomic_uint_least64_t *)ttak_dangerous_calloc(TTAK_SHARD_PAGE_SIZE, sizeof(atomic_uint_least64_t));
        if (!new_page) return NULL;
        
        atomic_uint_least64_t *expected = NULL;
        if (atomic_compare_exchange_weak_explicit(&self->shards.dir[page_idx], &expected, new_page, memory_order_release, memory_order_acquire)) {
            page = new_page;
            /* Update high water mark for fast cleanup */
            uint32_t current_active = atomic_load_explicit(&self->shards.active_pages, memory_order_relaxed);
            while (page_idx >= current_active && !atomic_compare_exchange_weak_explicit(&self->shards.active_pages, &current_active, page_idx + 1, memory_order_relaxed, memory_order_relaxed));
        } else {
            ttak_dangerous_free((void *)new_page);
            page = expected;
        }
    }
    return &page[slot_idx];
}

/* Internal payload header... */
typedef struct {
    size_t size;
    uint64_t ts;
#if defined(__GNUC__) || defined(__clang__)
    uint8_t data[] __attribute__((aligned(TTAK_CACHE_LINE_SIZE)));
#elif defined(_MSC_VER)
    __declspec(align(64)) uint8_t data[];
#else
    uint8_t data[];
#endif
} ttak_payload_header_t;

#define TTAK_GET_HEADER(ptr) ((ttak_payload_header_t *)((uint8_t *)(ptr) - offsetof(ttak_payload_header_t, data)))

static void _ttak_shared_payload_free(void *ptr) {
    if (!ptr) return;
    ttak_payload_header_t *header = TTAK_GET_HEADER(ptr);
    ttak_mem_free(header);
}

/* Internal helper for O(1) ownership validation */
static ttak_shared_result_t _ttak_shared_validate_owner(ttak_shared_t *self, ttak_owner_t *claimant, uint64_t current_ts) {
    if (self->level == TTAK_SHARED_NO_LEVEL) {
        return TTAK_OWNER_SUCCESS;
    }

    if (!claimant) {
        return TTAK_OWNER_INVALID;
    }

    /* 
     * O(1) bit test using dynamic mask.
     * [PERFORMANCE OPTIMIZATION] Only enforce full mask check on Level 3.
     */
    if (self->level >= TTAK_SHARED_LEVEL_3) {
        if (!ttak_dynamic_mask_test(&self->owners_mask, claimant->id)) {
            return TTAK_OWNER_INVALID;
        }
    }

    /* 
     * [MATHEMATICAL MODIFICATION: FAST-PATH SYNC]
     * If the shared global timestamp is already ahead, no need to check the lattice.
     */
    if (TTAK_LIKELY(self->ts >= current_ts)) {
        return TTAK_OWNER_SUCCESS;
    }

    if (self->level >= TTAK_SHARED_LEVEL_2) {
        /* 
         * [CORE LOGIC: STRIPED VERSION CHECK]
         * Optimized shard access with minimal atomic overhead.
         */
        atomic_uint_least64_t *shard = _ttak_shared_get_shard(self);
        
        if (TTAK_LIKELY(shard != NULL)) {
            /* Check only our assigned shard */
            if (atomic_load_explicit(shard, memory_order_relaxed) >= current_ts)
                return TTAK_OWNER_SUCCESS;
        }

        return TTAK_OWNER_CORRUPTED;
    }

    return TTAK_OWNER_SUCCESS;
}

/**
 * @brief Default implementation of the allocate method.
 */
static ttak_shared_result_t ttak_shared_allocate_impl(ttak_shared_t *self, size_t size, ttak_shared_level_t level) {
    if (!self || size == 0) return TTAK_OWNER_INVALID;

    uint64_t now = ttak_get_tick_count();
    size_t total_size = sizeof(ttak_payload_header_t) + size;
    
    ttak_payload_header_t *header = ttak_mem_alloc(total_size, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!header) return TTAK_OWNER_SHARE_DENIED;

    header->size = size;
    header->ts = ttak_get_tick_count_ns();
    
    atomic_init(&self->shared, header->data);
    self->cleanup = _ttak_shared_payload_free;
    self->size = size;
    self->level = level;
    self->status = TTAK_SHARED_READY;
    self->ts = header->ts;
    self->type_name = "raw_buffer";
    atomic_init(&self->retired_ptr, (void *)0);

    return TTAK_OWNER_SUCCESS;
}

static ttak_shared_result_t ttak_shared_allocate_typed_impl(ttak_shared_t *self, size_t size, const char *type_name, ttak_shared_level_t level) {
    ttak_shared_result_t res = ttak_shared_allocate_impl(self, size, level);
    if (res == TTAK_OWNER_SUCCESS) {
        self->type_name = type_name;
    }
    return res;
}

static ttak_shared_result_t ttak_shared_add_owner_impl(ttak_shared_t *self, ttak_owner_t *owner) {
    if (!self || !owner) return TTAK_OWNER_INVALID;

    /* Set bit atomically in the mask */
    if (!ttak_dynamic_mask_set(&self->owners_mask, owner->id)) {
        return TTAK_OWNER_SHARE_DENIED;
    }

    /* In the lock-free model, per-owner sync timestamps are not needed */
    return TTAK_OWNER_SUCCESS;
}

static const void *ttak_shared_access_impl(ttak_shared_t *self, ttak_owner_t *claimant, ttak_shared_result_t *result) {
    if (TTAK_UNLIKELY(!self)) return NULL;

    /* [ULTRA-FAST LOCK-FREE PATH] */
    if (TTAK_UNLIKELY(self->is_atomic_read)) {
        ttak_rwlock_rdlock(&self->rwlock);
    }

    /* 
     * Use relaxed load first. If the pointer is the same as last time 
     * and global ts is consistent, we can skip barriers.
     */
    void *ptr = atomic_load_explicit(&self->shared, memory_order_relaxed);
    
    if (TTAK_LIKELY(self->level == TTAK_SHARED_NO_LEVEL)) {
        if (result) *result = TTAK_OWNER_SUCCESS;
        return ptr;
    }

    /* Escalating to acquire only for metadata consistency */
    atomic_thread_fence(memory_order_acquire);

    uint64_t current_ts = ptr ? TTAK_GET_HEADER(ptr)->ts : self->ts;
    ttak_shared_result_t res = _ttak_shared_validate_owner(self, claimant, current_ts);
    if (result) *result = res;

    if (TTAK_UNLIKELY(res != TTAK_OWNER_SUCCESS && self->level == TTAK_SHARED_LEVEL_3)) {
        if (self->is_atomic_read) ttak_rwlock_unlock(&self->rwlock);
        return NULL;
    }

    return ptr;
}

static const void *ttak_shared_access_ebr_impl(ttak_shared_t *self, ttak_owner_t *claimant, bool protected, ttak_shared_result_t *result) {
    if (TTAK_UNLIKELY(!self)) return NULL;

    if (TTAK_LIKELY(protected)) {
        ttak_epoch_enter();
    }

    void *ptr = atomic_load_explicit(&self->shared, memory_order_relaxed);
    
    if (TTAK_LIKELY(self->level == TTAK_SHARED_NO_LEVEL)) {
        if (result) *result = TTAK_OWNER_SUCCESS;
        return ptr;
    }

    atomic_thread_fence(memory_order_acquire);

    uint64_t current_ts = ptr ? TTAK_GET_HEADER(ptr)->ts : self->ts;
    ttak_shared_result_t res = _ttak_shared_validate_owner(self, claimant, current_ts);
    if (result) *result = res;

    if (TTAK_UNLIKELY(res != TTAK_OWNER_SUCCESS && self->level == TTAK_SHARED_LEVEL_3)) {
        if (protected) ttak_epoch_exit();
        return NULL;
    }

    return ptr;
}

static void ttak_shared_release_impl(ttak_shared_t *self) {
    if (!self) return;
    if (self->is_atomic_read) {
        ttak_rwlock_unlock(&self->rwlock);
    }
}

static void ttak_shared_release_ebr_impl(ttak_shared_t *self) {
    (void)self;
    /* Assumes caller used protected=true in access_ebr */
    ttak_epoch_exit();
}

/**
 * @brief Default implementation of the sync_all method.
 */
static ttak_shared_result_t ttak_shared_sync_all_impl(ttak_shared_t *self, ttak_owner_t *claimant, int *affected) {
    if (!self) return TTAK_OWNER_INVALID;

    /* Check owner permission first (uses Lattice row-read) */
    ttak_shared_result_t res = _ttak_shared_validate_owner(self, claimant, self->ts);
    if (res != TTAK_OWNER_SUCCESS && self->level == TTAK_SHARED_LEVEL_3) {
        return res;
    }

    /* Update global timestamp and clear dirty flag */
    uint64_t new_ts = ttak_get_tick_count_ns();
    self->ts = new_ts;
    self->status &= ~TTAK_SHARED_DIRTY;

    /* 
     * [CORE LOGIC: STRIPED SHARD UPDATE]
     * Update only our assigned shard (O(1)).
     */
    atomic_uint_least64_t *shard = _ttak_shared_get_shard(self);
    if (TTAK_LIKELY(shard != NULL)) {
        atomic_store_explicit(shard, new_ts, memory_order_release);
    }

    if (affected) *affected = self->owners_mask.count;

    return TTAK_OWNER_SUCCESS;
}

static ttak_shared_result_t ttak_shared_set_ro_impl(ttak_shared_t *self) {
    if (!self) return TTAK_OWNER_INVALID;
    ttak_rwlock_wrlock(&self->rwlock);
    self->status |= TTAK_SHARED_READONLY;
    ttak_rwlock_unlock(&self->rwlock);
    return TTAK_OWNER_SUCCESS;
}

static ttak_shared_result_t ttak_shared_set_rw_impl(ttak_shared_t *self) {
    if (!self) return TTAK_OWNER_INVALID;
    ttak_rwlock_wrlock(&self->rwlock);
    self->status &= ~TTAK_SHARED_READONLY;
    ttak_rwlock_unlock(&self->rwlock);
    return TTAK_OWNER_SUCCESS;
}

static ttak_shared_result_t ttak_shared_set_atomic_read_impl(ttak_shared_t *self, bool enable) {
    if (!self) return TTAK_OWNER_INVALID;
    self->is_atomic_read = enable;
    return TTAK_OWNER_SUCCESS;
}

/* Helper for retired container cleanup */
static void _ttak_shared_container_cleanup(void *ptr) {
    ttak_shared_t *self = (ttak_shared_t *)ptr;
    if (!self) return;

    /* Free resources other than self->shared (which is already retired) */
    ttak_dynamic_mask_destroy(&self->owners_mask);
    
    uint32_t active = atomic_load_explicit(&self->shards.active_pages, memory_order_relaxed);
    for (uint32_t i = 0; i < active; i++) {
        atomic_uint_least64_t *page = atomic_load_explicit(&self->shards.dir[i], memory_order_relaxed);
        if (page) ttak_dangerous_free((void *)page);
    }
    
    ttak_rwlock_destroy(&self->rwlock);
    
    /* Finally free the container itself */
    ttak_mem_free(self);
}

static void ttak_shared_retire_impl(ttak_shared_t *self) {
    if (!self) return;

    /* Retire the internal data if it exists */
    void *ptr = atomic_load(&self->shared);
    if (ptr) {
        ttak_epoch_retire(ptr, self->cleanup ? self->cleanup : ttak_mem_free);
        atomic_store(&self->shared, (void *)0);
    }

    /* Retire the container itself */
    ttak_epoch_retire(self, _ttak_shared_container_cleanup);
}

/**
 * @brief Constructor for ttak_shared_t.
 * @note This initializes the function pointers to default implementations.
 */
void ttak_shared_init(ttak_shared_t *self) {
    if (!self) return;

    /* Bind implementations */
    self->allocate = ttak_shared_allocate_impl;
    self->allocate_typed = ttak_shared_allocate_typed_impl;
    self->add_owner = ttak_shared_add_owner_impl;
    self->access = ttak_shared_access_impl;
    self->access_ebr = ttak_shared_access_ebr_impl;
    self->release = ttak_shared_release_impl;
    self->release_ebr = ttak_shared_release_ebr_impl;
    self->sync_all = ttak_shared_sync_all_impl;
    self->set_ro = ttak_shared_set_ro_impl;
    self->set_rw = ttak_shared_set_rw_impl;
    self->set_atomic_read = ttak_shared_set_atomic_read_impl;
    self->retire = ttak_shared_retire_impl;

    ttak_rwlock_init(&self->rwlock);
    ttak_dynamic_mask_init(&self->owners_mask);
    
    atomic_init(&self->shared, (void *)0);
    atomic_init(&self->retired_ptr, (void *)0);
    
    self->status = TTAK_SHARED_READY;
    self->level = TTAK_SHARED_LEVEL_1;
    self->is_atomic_read = false;
    self->size = 0;
    self->ts = 0;
    self->cleanup = NULL;
    self->type_name = NULL;

    /* Initialize Lock-Free Shards */
    for (int i = 0; i < TTAK_SHARD_DIR_SIZE; i++) {
        atomic_init(&self->shards.dir[i], NULL);
    }
    atomic_init(&self->shards.active_pages, 0);
}

/**
 * @brief Destructor for ttak_shared_t.
 */
void ttak_shared_destroy(ttak_shared_t *self) {
    if (!self) return;

    ttak_rwlock_wrlock(&self->rwlock);
    
    void *ptr = atomic_load(&self->shared);
    if (self->cleanup && ptr) {
        self->cleanup(ptr);
    } else if (ptr) {
        ttak_mem_free(ptr);
    }

    ttak_dynamic_mask_destroy(&self->owners_mask);
    
    uint32_t active = atomic_load_explicit(&self->shards.active_pages, memory_order_relaxed);
    for (uint32_t i = 0; i < active; i++) {
        atomic_uint_least64_t *page = atomic_load_explicit(&self->shards.dir[i], memory_order_relaxed);
        if (page) ttak_dangerous_free((void *)page);
    }
    
    ttak_rwlock_unlock(&self->rwlock);
    ttak_rwlock_destroy(&self->rwlock);
}

ttak_shared_result_t ttak_shared_swap_ebr(ttak_shared_t *self, void *new_shared, size_t new_size) {
    if (!self || !new_shared) return TTAK_OWNER_INVALID;

    /* 1. Create new implicit payload */
    size_t total_size = sizeof(ttak_payload_header_t) + new_size;
    ttak_payload_header_t *header = ttak_mem_alloc(total_size, __TTAK_UNSAFE_MEM_FOREVER__, ttak_get_tick_count());
    if (!header) return TTAK_OWNER_SHARE_DENIED;

    header->size = new_size;
    header->ts = ttak_get_tick_count_ns();
    
    /* Copy from raw buffer to payload */
    memcpy(header->data, new_shared, new_size);

    ttak_rwlock_wrlock(&self->rwlock);
    self->status |= TTAK_SHARED_SWAPPING;

    /* 2. Move existing shared pointer (with its header) to retirement queue */
    void *old_ptr = atomic_load(&self->shared);
    if (old_ptr) {
        /* self->cleanup is expected to be _ttak_shared_payload_free */
        ttak_epoch_retire(old_ptr, self->cleanup ? self->cleanup : ttak_mem_free);
    }

    /* 3. Atomic pointer swap (Release barrier ensures header content is visible) */
    atomic_store_explicit(&self->shared, header->data, memory_order_release);
    
    /* 4. Update shadow members for rough access */
    self->size = new_size;
    self->ts = header->ts;
    self->cleanup = _ttak_shared_payload_free;

    self->status &= ~TTAK_SHARED_SWAPPING;
    self->status |= TTAK_SHARED_DIRTY;
    ttak_rwlock_unlock(&self->rwlock);

    return TTAK_OWNER_SUCCESS;
}

size_t ttak_shared_get_payload_size(const void *ptr) {
    if (!ptr) return 0;
    return TTAK_GET_HEADER(ptr)->size;
}

uint64_t ttak_shared_get_payload_ts(const void *ptr) {
    if (!ptr) return 0;
    return TTAK_GET_HEADER(ptr)->ts;
}

