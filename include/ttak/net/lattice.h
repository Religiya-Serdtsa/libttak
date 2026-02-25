#ifndef TTAK_NET_LATTICE_H
#define TTAK_NET_LATTICE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ttak/mem/mem.h>
#include <ttak/sync/sync.h>
#include <ttak/atomic/atomic.h>

/**
 * @file lattice.h
 * @brief Choi Seok-jeong's Lattice (Sanpan) for Lock-Free Parallel Ingress.
 */

#define TTAK_LATTICE_SLOT_SIZE 2048
#define TTAK_LATTICE_MAX_DIM 16

/**
 * @brief A single data slot in the lattice.
 */
typedef struct ttak_net_lattice_slot {
    _Alignas(64) uint8_t  data[TTAK_LATTICE_SLOT_SIZE];
    uint64_t timestamp;
    uint32_t len;
    uint32_t seq;
    volatile uint64_t state;
    uint8_t padding[32]; /* Pad to ensure 64-byte multiple if needed */
} ttak_net_lattice_slot_t;

/**
 * @brief The Lattice (Sanpan) structure.
 */
typedef struct ttak_net_lattice {
    uint32_t dim;      /* Dimension of the square (must be power of 2) */
    uint32_t mask;     /* dim - 1 */
    ttak_net_lattice_slot_t *slots; /* dim * dim array */
    volatile uint64_t total_ingress;
} ttak_net_lattice_t;

/**
 * @brief Initializes a net lattice with given dimension.
 */
ttak_net_lattice_t* ttak_net_lattice_create(uint32_t dim, uint64_t now);

/**
 * @brief Destroys the lattice.
 */
void ttak_net_lattice_destroy(ttak_net_lattice_t *lat, uint64_t now);

/**
 * @brief Lock-free deterministic write inspired by Choi Seok-jeong's Lattice.
 */
_Bool ttak_net_lattice_write(ttak_net_lattice_t *lat, uint32_t tid, const void *data, uint32_t len, uint64_t now);

/**
 * @brief Reads data from a designated slot in the lattice for a specific worker (tid).
 */
_Bool ttak_net_lattice_read(ttak_net_lattice_t *lat, uint32_t tid, void *dst, uint32_t *len_out, uint64_t now);

/**
 * @brief Returns the global default lattice.
 */
ttak_net_lattice_t* ttak_net_lattice_get_default(void);

/**
 * @brief Sets the worker ID for the current thread to be used in deterministic lattice selection.
 */
void ttak_net_lattice_set_worker_id(uint32_t tid);

/**
 * @brief Gets the worker ID for the current thread.
 */
uint32_t ttak_net_lattice_get_worker_id(void);

#endif /* TTAK_NET_LATTICE_H */
