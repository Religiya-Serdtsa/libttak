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
#include <stdatomic.h>

/* Internal thread-local ID for Latin Square isolation */
static _Thread_local int ttak_tid = -1;
static atomic_int ttak_tid_counter = ATOMIC_VAR_INIT(0);

static inline int get_ttak_tid() {
    if (ttak_tid == -1) {
        ttak_tid = atomic_fetch_add(&ttak_tid_counter, 1) & TTAK_LATTICE_MASK;
    }
    return ttak_tid;
}

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
        /* 
         * [CORE LOGIC: LATIN SQUARE BROADCAST]
         * By reading one row in the lattice, we intersect all diagonals.
         * This ensures we see the latest sync timestamp from any worker thread
         * without using any locks or global barriers.
         */
        int tid = get_ttak_tid();
        uint64_t max_sync_ts = 0;
        for (int c = 0; c < TTAK_LATTICE_SIZE; c++) {
            uint64_t ts = atomic_load_explicit(&self->lattice.slots[tid][c], memory_order_acquire);
            if (ts > max_sync_ts) max_sync_ts = ts;
        }

        if (max_sync_ts < current_ts) {
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

    /* In the lock-free Lattice model, per-owner sync timestamps are not needed */
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

    /* Atomic load with acquire barrier to see the header contents */
    void *ptr = atomic_load_explicit(&self->shared, memory_order_acquire);
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
    void *ptr = atomic_load_explicit(&self->shared, memory_order_acquire);
    uint64_t current_ts = ptr ? TTAK_GET_HEADER(ptr)->ts : self->ts;

    ttak_shared_result_t res = _ttak_shared_validate_owner(self, claimant, current_ts);
    if (result) *result = res;

    if (res != TTAK_OWNER_SUCCESS && self->level == TTAK_SHARED_LEVEL_3) {
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
     * [CORE LOGIC: LATIN SQUARE DIAGONAL WRITE]
     * Instead of iterating over all owners (O(N)), we update only our assigned 
     * diagonal in the lattice (O(LATTICE_SIZE)).
     * This 'broadcast' is eventually seen by all owners when they read a row.
     */
    int tid = get_ttak_tid();
    for (int r = 0; r < TTAK_LATTICE_SIZE; r++) {
        int c = (tid - r + TTAK_LATTICE_SIZE) & TTAK_LATTICE_MASK;
        atomic_store_explicit(&self->lattice.slots[r][c], new_ts, memory_order_release);
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

    memset(self, 0, sizeof(ttak_shared_t));
    
    ttak_rwlock_init(&self->rwlock);
    ttak_dynamic_mask_init(&self->owners_mask);
    atomic_init(&self->retired_ptr, (void *)0);

    /* Initialize Latin Square Lattice */
    for (int r = 0; r < TTAK_LATTICE_SIZE; r++) {
        for (int c = 0; c < TTAK_LATTICE_SIZE; c++) {
            atomic_init(&self->lattice.slots[r][c], 0);
        }
    }

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
    
    void *ptr = atomic_load(&self->shared);
    if (self->cleanup && ptr) {
        self->cleanup(ptr);
    } else if (ptr) {
        ttak_mem_free(ptr);
    }

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

