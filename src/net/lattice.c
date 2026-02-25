#include <ttak/net/lattice.h>
#include <ttak/mem/mem.h>
#include <ttak/atomic/atomic.h>
#include <ttak/timing/timing.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

static _Thread_local uint32_t tls_worker_id = 0;
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

    lat->dim = dim;
    lat->mask = dim - 1;
    lat->total_ingress = 0;
    
    size_t slots_count = (size_t)dim * (size_t)dim;
    lat->slots = ttak_mem_alloc(slots_count * sizeof(ttak_net_lattice_slot_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!lat->slots) {
        ttak_mem_free(lat);
        return NULL;
    }
    
    memset(lat->slots, 0, slots_count * sizeof(ttak_net_lattice_slot_t));
    return lat;
}

void ttak_net_lattice_destroy(ttak_net_lattice_t *lat, uint64_t now) {
    if (!lat) return;
    if (lat->slots) ttak_mem_free(lat->slots);
    ttak_mem_free(lat);
    (void)now;
}

/**
 * @brief Lock-free deterministic write inspired by Choi Seok-jeong's Lattice.
 */
_Bool ttak_net_lattice_write(ttak_net_lattice_t *lat, uint32_t tid, const void *data, uint32_t len, uint64_t now) {
    if (!lat || !data || len > TTAK_LATTICE_SLOT_SIZE) return false;
    
    uint32_t dim = lat->dim;
    uint32_t mask = lat->mask;
    uint32_t my_tid = tid & mask;
    
    /* Iterate through the Sanpan (Counting Board) lattice */
    for (uint32_t r = 0; r < dim; r++) {
        for (uint32_t c = 0; c < dim; c++) {
            /* Orthogonality check: Mathematical isolation */
            if (((r + c) & mask) == my_tid) {
                ttak_net_lattice_slot_t *slot = &lat->slots[r * dim + c];
                
                if (ttak_atomic_read64(&slot->state) == 0) {
                    ttak_atomic_write64(&slot->state, 1); /* Entering writing state */
                    
                    memcpy(slot->data, data, len);
                    slot->len = len;
                    slot->timestamp = now;
                    slot->seq++;
                    
                    ttak_atomic_write64(&slot->state, 2); /* Ready state */
                    ttak_atomic_add64(&lat->total_ingress, 1);
                    return true;
                }
            }
        }
    }
    
    return false;
}

_Bool ttak_net_lattice_read(ttak_net_lattice_t *lat, uint32_t tid, void *dst, uint32_t *len_out, uint64_t now) {
    if (!lat || !dst || !len_out) return false;
    (void)now;
    
    uint32_t dim = lat->dim;
    uint32_t mask = lat->mask;
    uint32_t my_tid = tid & mask;
    
    for (uint32_t r = 0; r < dim; r++) {
        for (uint32_t c = 0; c < dim; c++) {
            if (((r + c) & mask) == my_tid) {
                ttak_net_lattice_slot_t *slot = &lat->slots[r * dim + c];
                
                if (ttak_atomic_read64(&slot->state) == 2) {
                    ttak_atomic_write64(&slot->state, 3); /* Mark as reading/processing */
                    
                    uint32_t len = slot->len;
                    memcpy(dst, slot->data, len);
                    *len_out = len;
                    
                    ttak_atomic_write64(&slot->state, 0); /* Reset to empty */
                    return true;
                }
            }
        }
    }
    
    return false;
}
