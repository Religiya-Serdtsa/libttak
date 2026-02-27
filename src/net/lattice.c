#include <ttak/net/lattice.h>
#include <ttak/mem/mem.h>
#include <ttak/atomic/atomic.h>
#include <ttak/timing/timing.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#if defined(__TINYC__)
static uint32_t tls_worker_id = 0;
#else
static _Thread_local uint32_t tls_worker_id = 0;
#endif
static ttak_net_lattice_t *global_default_lattice = NULL;
static pthread_once_t lattice_once = PTHREAD_ONCE_INIT;

static void lattice_init_default(void) {
    /* Auto-detect CPU count or use a safe default (4) for lattice dimension */
    uint32_t dim = 4;
    global_default_lattice = ttak_net_lattice_create(dim, ttak_get_tick_count());
}

ttak_net_lattice_t* ttak_net_lattice_get_default(void) {
    pthread_once(&lattice_once, lattice_init_default);
    return global_default_lattice;
}

void ttak_net_lattice_set_worker_id(uint32_t tid) {
    tls_worker_id = tid;
}

uint32_t ttak_net_lattice_get_worker_id(void) {
    return tls_worker_id;
}

ttak_net_lattice_t* ttak_net_lattice_create(uint32_t dim, uint64_t now) {
    /* Ensure dim is power of 2 for masking */
    if (dim == 0 || (dim & (dim - 1)) != 0 || dim > TTAK_LATTICE_MAX_DIM) return NULL;

    ttak_net_lattice_t *lat = ttak_mem_alloc(sizeof(ttak_net_lattice_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!lat) return NULL;

    size_t slots_count = (size_t)dim * (size_t)dim;
    lat->slots = ttak_mem_alloc(slots_count * sizeof(ttak_net_lattice_slot_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!lat->slots) {
        ttak_mem_free(lat);
        return NULL;
    }

    lat->dim = dim;
    lat->mask = dim - 1;
    lat->capacity = (uint32_t)slots_count;
    lat->total_ingress = 0;
    lat->used_slots = 0;
    lat->is_full = false;
    lat->next = NULL;
    if (ttak_mutex_init(&lat->expand_lock) != 0) {
        ttak_mem_free(lat->slots);
        ttak_mem_free(lat);
        return NULL;
    }

    memset(lat->slots, 0, slots_count * sizeof(ttak_net_lattice_slot_t));
    (void)now;
    return lat;
}

void ttak_net_lattice_destroy(ttak_net_lattice_t *lat, uint64_t now) {
    (void)now;
    while (lat) {
        ttak_net_lattice_t *next = lat->next;
        if (lat->slots) {
            ttak_mem_free(lat->slots);
        }
        ttak_mutex_destroy(&lat->expand_lock);
        ttak_mem_free(lat);
        lat = next;
    }
}

/**
 * @brief Lock-free deterministic write inspired by Choi Seok-jeong's Lattice.
 */
_Bool ttak_net_lattice_write(ttak_net_lattice_t *lat, uint32_t tid, const void *data, uint32_t len, uint64_t now) {
    if (!lat || !data || len > TTAK_LATTICE_SLOT_SIZE) return false;
    
    uint32_t mask = lat->mask;
    uint32_t my_tid = tid & mask;
    
    for (ttak_net_lattice_t *node = lat; node; ) {
        uint32_t dim = node->dim;
        /* Iterate through the Sanpan (Counting Board) lattice */
        for (uint32_t r = 0; r < dim; r++) {
            for (uint32_t c = 0; c < dim; c++) {
                /* Orthogonality check: Mathematical isolation */
                if (((r + c) & mask) == my_tid) {
                    ttak_net_lattice_slot_t *slot = &node->slots[r * dim + c];
                    
                    if (ttak_atomic_read64(&slot->state) == 0) {
                        ttak_atomic_write64(&slot->state, 1); /* Entering writing state */
                        
                        memcpy(slot->data, data, len);
                        slot->len = len;
                        slot->timestamp = now;
                        slot->seq++;
                        
                        ttak_atomic_write64(&slot->state, 2); /* Ready state */
                        ttak_atomic_add64(&node->total_ingress, 1);
                        ttak_net_lattice_mark_slot_acquired(node, now);
                        return true;
                    }
                }
            }
        }

        ttak_net_lattice_t *next = node->next;
        if (!next) {
            next = ttak_net_lattice_ensure_next(node, now);
        }
        node = next;
    }
    
    return false;
}

_Bool ttak_net_lattice_read(ttak_net_lattice_t *lat, uint32_t tid, void *dst, uint32_t *len_out, uint64_t now) {
    if (!lat || !dst || !len_out) return false;
    (void)now;
    
    uint32_t mask = lat->mask;
    uint32_t my_tid = tid & mask;
    
    for (ttak_net_lattice_t *node = lat; node; node = node->next) {
        uint32_t dim = node->dim;
        for (uint32_t r = 0; r < dim; r++) {
            for (uint32_t c = 0; c < dim; c++) {
                if (((r + c) & mask) == my_tid) {
                    ttak_net_lattice_slot_t *slot = &node->slots[r * dim + c];
                    
                    if (ttak_atomic_read64(&slot->state) == 2) {
                        ttak_atomic_write64(&slot->state, 3); /* Mark as reading/processing */
                        
                        uint32_t len = slot->len;
                        memcpy(dst, slot->data, len);
                        *len_out = len;
                        
                        ttak_atomic_write64(&slot->state, 0); /* Reset to empty */
                        ttak_net_lattice_mark_slot_released(node);
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

ttak_net_lattice_t* ttak_net_lattice_ensure_next(ttak_net_lattice_t *lat, uint64_t now) {
    if (!lat) return NULL;
    if (lat->next) return lat->next;

    ttak_mutex_lock(&lat->expand_lock);
    if (!lat->next) {
        lat->next = ttak_net_lattice_create(lat->dim, now);
    }
    ttak_mutex_unlock(&lat->expand_lock);
    return lat->next;
}

void ttak_net_lattice_mark_slot_acquired(ttak_net_lattice_t *lat, uint64_t now) {
    if (!lat) return;
    uint64_t used = ttak_atomic_add64(&lat->used_slots, 1);
    if (used >= lat->capacity) {
        lat->is_full = true;
    }
    uint64_t scaled = used * 100;
    uint64_t threshold = (uint64_t)lat->capacity * 80;
    if (!lat->next && scaled >= threshold) {
        ttak_net_lattice_ensure_next(lat, now);
    }
}

void ttak_net_lattice_mark_slot_released(ttak_net_lattice_t *lat) {
    if (!lat) return;
    uint64_t remaining = ttak_atomic_sub64(&lat->used_slots, 1);
    if (remaining < lat->capacity) {
        lat->is_full = false;
    }
}
