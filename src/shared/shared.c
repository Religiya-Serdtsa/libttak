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
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* Internal payload header for atomic consistency of size, ts, and data */
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

/* Internal helper to ensure timestamp array capacity */
static bool _ttak_shared_ensure_ts_capacity(ttak_shared_t *self, uint32_t owner_id) {
    if (owner_id < self->ts_capacity) {
        return true;
    }

    uint32_t new_capacity = ((owner_id / 64) + 1) * 64;
    uint64_t now = ttak_get_tick_count();
    uint64_t *new_ts = ttak_mem_realloc(self->last_sync_ts, sizeof(uint64_t) * new_capacity, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!new_ts) return false;

    memset(new_ts + self->ts_capacity, 0, sizeof(uint64_t) * (new_capacity - self->ts_capacity));
    self->last_sync_ts = new_ts;
    self->ts_capacity = new_capacity;

    return true;
}

/* Internal helper for O(1) ownership validation */
static ttak_shared_result_t _ttak_shared_validate_owner(ttak_shared_t *self, ttak_owner_t *claimant, uint64_t current_ts) {
    if (self->level == TTAK_SHARED_NO_LEVEL) {
        return TTAK_OWNER_SUCCESS;
    }

    if (!claimant) {
        return TTAK_OWNER_INVALID;
    }

    /* O(1) bit test using dynamic mask */
    if (!ttak_dynamic_mask_test(&self->owners_mask, claimant->id)) {
        return TTAK_OWNER_INVALID;
    }

    if (self->level >= TTAK_SHARED_LEVEL_2) {
        /* Check for timestamp drift or corruption */
        /* Note: accessing last_sync_ts should ideally be inside self->rwlock */
        if (claimant->id >= self->ts_capacity || self->last_sync_ts[claimant->id] < current_ts) {
            return TTAK_OWNER_CORRUPTED;
        }
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
    
    self->shared = header->data;
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

    /* Update sync timestamp under shared lock */
    ttak_rwlock_wrlock(&self->rwlock);
    if (!_ttak_shared_ensure_ts_capacity(self, owner->id)) {
        ttak_rwlock_unlock(&self->rwlock);
        return TTAK_OWNER_SHARE_DENIED;
    }
    self->last_sync_ts[owner->id] = self->ts;
    ttak_rwlock_unlock(&self->rwlock);

    return TTAK_OWNER_SUCCESS;
}

/**
 * @brief Default implementation of the access method.
 */
static const void *ttak_shared_access_impl(ttak_shared_t *self, ttak_owner_t *claimant, ttak_shared_result_t *result) {
    if (!self) return NULL;

    if (self->is_atomic_read) {
        ttak_rwlock_rdlock(&self->rwlock);
    }

    /* Rough load */
    void *ptr = self->shared;
    uint64_t current_ts = ptr ? TTAK_GET_HEADER(ptr)->ts : self->ts;

    ttak_shared_result_t res = _ttak_shared_validate_owner(self, claimant, current_ts);
    if (result) *result = res;

    if (res != TTAK_OWNER_SUCCESS && self->level == TTAK_SHARED_LEVEL_3) {
        if (self->is_atomic_read) {
            ttak_rwlock_unlock(&self->rwlock);
        }
        return NULL;
    }

    return ptr;
}

/**
 * @brief EBR-aware access implementation.
 */
static const void *ttak_shared_access_ebr_impl(ttak_shared_t *self, ttak_owner_t *claimant, bool protected, ttak_shared_result_t *result) {
    if (!self) return NULL;

    if (protected) {
        ttak_epoch_enter();
    }

    /* Atomic load with acquire barrier to see the header contents */
    void *ptr = atomic_load_explicit((void * _Atomic *)&self->shared, memory_order_acquire);
    uint64_t current_ts = ptr ? TTAK_GET_HEADER(ptr)->ts : self->ts;

    ttak_shared_result_t res = _ttak_shared_validate_owner(self, claimant, current_ts);
    if (result) *result = res;

    if (res != TTAK_OWNER_SUCCESS && self->level == TTAK_SHARED_LEVEL_3) {
        if (protected) ttak_epoch_exit();
        return NULL;
    }

    /* 
     * In EBR mode, even if swap_ebr happens concurrently, the old pointer 
     * (which we just loaded) is guaranteed to remain valid until we exit.
     * The metadata (size, ts) can be retrieved from TTAK_GET_HEADER(ptr).
     */
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

    ttak_rwlock_wrlock(&self->rwlock);

    ttak_shared_result_t res = _ttak_shared_validate_owner(self, claimant, self->ts);
    if (res != TTAK_OWNER_SUCCESS && self->level == TTAK_SHARED_LEVEL_3) {
        ttak_rwlock_unlock(&self->rwlock);
        return res;
    }

    /* Update global timestamp and clear dirty flag */
    self->ts = ttak_get_tick_count_ns();
    self->status &= ~TTAK_SHARED_DIRTY;

    int count = 0;
    
    /* Iterate over the mask safely */
    ttak_rwlock_rdlock(&self->owners_mask.lock);
    for (uint32_t i = 0; i < self->owners_mask.capacity / 64; ++i) {
        uint64_t mask = self->owners_mask.bits[i];
        if (mask == 0) continue;

        for (int b = 0; b < 64; ++b) {
            if (mask & (1ULL << b)) {
                uint32_t owner_id = i * 64 + b;
                if (owner_id < self->ts_capacity) {
                    self->last_sync_ts[owner_id] = self->ts;
                    count++;
                }
            }
        }
    }
    ttak_rwlock_unlock(&self->owners_mask.lock);

    if (affected) *affected = count;

    ttak_rwlock_unlock(&self->rwlock);
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
    ttak_mem_free(self->last_sync_ts);
    ttak_dynamic_mask_destroy(&self->owners_mask);
    ttak_rwlock_destroy(&self->rwlock);
    
    /* Finally free the container itself */
    ttak_mem_free(self);
}

static void ttak_shared_retire_impl(ttak_shared_t *self) {
    if (!self) return;

    /* Retire the internal data if it exists */
    if (self->shared) {
        ttak_epoch_retire(self->shared, self->cleanup ? self->cleanup : ttak_mem_free);
        self->shared = NULL; 
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

    memset(self, 0, sizeof(ttak_shared_t));
    
    ttak_rwlock_init(&self->rwlock);
    ttak_dynamic_mask_init(&self->owners_mask);
    atomic_init(&self->retired_ptr, (void *)0);

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
}

/**
 * @brief Destructor for ttak_shared_t.
 */
void ttak_shared_destroy(ttak_shared_t *self) {
    if (!self) return;

    ttak_rwlock_wrlock(&self->rwlock);
    
    if (self->cleanup && self->shared) {
        self->cleanup(self->shared);
    } else if (self->shared) {
        ttak_mem_free(self->shared);
    }

    ttak_mem_free(self->last_sync_ts);
    ttak_dynamic_mask_destroy(&self->owners_mask);
    
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
    
    /* Using optimized duplication if possible, but here we are copying from raw buffer to payload */
    memcpy(header->data, new_shared, new_size);

    ttak_rwlock_wrlock(&self->rwlock);
    self->status |= TTAK_SHARED_SWAPPING;

    /* 2. Move existing shared pointer (with its header) to retirement queue */
    if (self->shared) {
        /* self->cleanup is expected to be _ttak_shared_payload_free */
        ttak_epoch_retire(self->shared, self->cleanup ? self->cleanup : ttak_mem_free);
    }

    /* 3. Atomic pointer swap (Release barrier ensures header content is visible) */
    atomic_store_explicit((void * _Atomic *)&self->shared, header->data, memory_order_release);
    
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

