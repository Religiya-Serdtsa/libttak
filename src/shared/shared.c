/**
 * @file ttak_shared.c
 * @brief Implementation of shared memory resource management with ttak_dynamic_mask_t.
 * @author Gemini
 * @date 2026-02-08
 */

#include <ttak/shared/shared.h>
#include <ttak/timing/timing.h>
#include <ttak/mem/mem.h>
#include <stdlib.h>
#include <string.h>

/* Internal helper to ensure timestamp array capacity */
static bool _ttak_shared_ensure_ts_capacity(ttak_shared_t *self, uint32_t owner_id) {
    if (owner_id < self->ts_capacity) {
        return true;
    }

    uint32_t new_capacity = ((owner_id / 64) + 1) * 64;
    uint64_t *new_ts = realloc(self->last_sync_ts, sizeof(uint64_t) * new_capacity);
    if (!new_ts) return false;

    memset(new_ts + self->ts_capacity, 0, sizeof(uint64_t) * (new_capacity - self->ts_capacity));
    self->last_sync_ts = new_ts;
    self->ts_capacity = new_capacity;

    return true;
}

/* Internal helper for O(1) ownership validation */
static ttak_shared_result_t _ttak_shared_validate_owner(ttak_shared_t *self, ttak_owner_t *claimant) {
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
        if (claimant->id >= self->ts_capacity || self->last_sync_ts[claimant->id] < self->ts) {
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
    self->shared = ttak_mem_alloc(size, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!self->shared) return TTAK_OWNER_SHARE_DENIED;

    self->cleanup = ttak_mem_free;
    self->size = size;
    self->level = level;
    self->status = TTAK_SHARED_READY;
    self->ts = ttak_get_tick_count_ns();
    self->type_name = "raw_buffer";

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

    ttak_shared_result_t res = _ttak_shared_validate_owner(self, claimant);
    if (result) *result = res;

    if (res != TTAK_OWNER_SUCCESS && self->level == TTAK_SHARED_LEVEL_3) {
        if (self->is_atomic_read) {
            ttak_rwlock_unlock(&self->rwlock);
        }
        return NULL;
    }

    return self->shared;
}

static void ttak_shared_release_impl(ttak_shared_t *self) {
    if (!self) return;
    if (self->is_atomic_read) {
        ttak_rwlock_unlock(&self->rwlock);
    }
}

/**
 * @brief Default implementation of the sync_all method.
 */
static ttak_shared_result_t ttak_shared_sync_all_impl(ttak_shared_t *self, ttak_owner_t *claimant, int *affected) {
    if (!self) return TTAK_OWNER_INVALID;

    ttak_rwlock_wrlock(&self->rwlock);

    ttak_shared_result_t res = _ttak_shared_validate_owner(self, claimant);
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

/**
 * @brief Constructor for ttak_shared_t.
 * @note This initializes the function pointers to default implementations.
 */
void ttak_shared_init(ttak_shared_t *self) {
    if (!self) return;

    memset(self, 0, sizeof(ttak_shared_t));
    
    ttak_rwlock_init(&self->rwlock);
    ttak_dynamic_mask_init(&self->owners_mask);

    /* Bind implementations */
    self->allocate = ttak_shared_allocate_impl;
    self->allocate_typed = ttak_shared_allocate_typed_impl;
    self->add_owner = ttak_shared_add_owner_impl;
    self->access = ttak_shared_access_impl;
    self->release = ttak_shared_release_impl;
    self->sync_all = ttak_shared_sync_all_impl;
    self->set_ro = ttak_shared_set_ro_impl;
    self->set_rw = ttak_shared_set_rw_impl;
    self->set_atomic_read = ttak_shared_set_atomic_read_impl;
}

/**
 * @brief Destructor for ttak_shared_t.
 */
void ttak_shared_destroy(ttak_shared_t *self) {
    if (!self) return;

    ttak_rwlock_wrlock(&self->rwlock);
    
    if (self->cleanup && self->shared) {
        self->cleanup(self->shared);
    } else {
        free(self->shared);
    }

    free(self->last_sync_ts);
    ttak_dynamic_mask_destroy(&self->owners_mask);
    
    ttak_rwlock_unlock(&self->rwlock);
    ttak_rwlock_destroy(&self->rwlock);
}
